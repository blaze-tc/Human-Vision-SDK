"""Generate real PyTorch versus ncnn Vulkan Body26 crop comparisons."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

import cv2
import numpy as np
import torch

from tools.models.ncnn.compare_pose_outputs import compare_pose
from tools.models.ncnn.model_contract import (activate_pinned_vendor, canonical_json,
                                              require_hash, sha256_file)


def decode(head, x: np.ndarray, y: np.ndarray, inverse: np.ndarray, bbox: list[float]) -> dict:
    points, scores = head.decoder.decode(x, y)
    points = np.asarray(points).reshape(26, 2).astype(np.float32)
    scores = np.asarray(scores).reshape(26).astype(np.float32)
    points = cv2.transform(points[None], inverse)[0]
    valid = (scores > 0) & (points[:, 0] >= bbox[0]) & (points[:, 1] >= bbox[1]) & (points[:, 0] <= bbox[2]) & (points[:, 1] <= bbox[3])
    return {"bbox": bbox, "points": points.tolist(), "scores": scores.tolist(),
            "valid": valid.tolist()}


def cases(image: np.ndarray, bbox: list[float]):
    height, width = image.shape[:2]
    yield "full-body", image, bbox
    yield "clipped-person", image, [0., bbox[1], min(width - 1., bbox[2] * .72), bbox[3]]
    yield "mirrored", cv2.flip(image, 1), [width - bbox[2], bbox[1], width - bbox[0], bbox[3]]
    yield "rotated", cv2.rotate(image, cv2.ROTATE_90_CLOCKWISE), [height - bbox[3], bbox[0], height - bbox[1], bbox[2]]


def run(checkpoint: Path, vendor_root: Path, image_path: Path, onnx: Path,
        param: Path, weights: Path, runner: Path, output: Path, adb: Path | None = None,
        runner_mode: str | None = None) -> dict:
    if runner_mode != 'strict-vkmat':
        raise ValueError("Unsupported pose runner mode")
    from tools.models.ncnn.build_local_eval_pack import (POSE_ONNX_SHA256, POSE_PARAM_SHA256, POSE_BIN_SHA256,
        POSE_RUNNER_SHA256, POSE_RUNNER_SOURCE_SHA256, parse_vkmat_audit)
    from tools.models.ncnn.model_contract import POSE_CHECKPOINT_SHA256
    for path, digest in ((onnx, POSE_ONNX_SHA256), (param, POSE_PARAM_SHA256), (weights, POSE_BIN_SHA256),
                         (image_path, "7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31")):
        require_hash(path, digest, "eligible golden input")
    if adb is None:
        raise ValueError("Eligibility requires the strict Android VkMat runner")
    require_hash(runner,POSE_RUNNER_SHA256,'strict VkMat runner')
    require_hash(Path(__file__).with_name('pose_golden_runner.cpp'),POSE_RUNNER_SOURCE_SHA256,'strict VkMat runner source')
    if output.exists() and any(output.iterdir()):
        raise ValueError("Use a new output directory so old evidence cannot satisfy the gate")
    require_hash(checkpoint, POSE_CHECKPOINT_SHA256, "official Body26 checkpoint")
    activate_pinned_vendor(vendor_root)
    from mmpose.apis import init_model
    from mmpose.structures.bbox import bbox_xyxy2cs, get_warp_matrix
    from mmpose.datasets.transforms.topdown_transforms import TopdownAffine
    config = vendor_root / "mmpose/projects/rtmpose/rtmpose/body_2d_keypoint/rtmpose-t_8xb1024-700e_body8-halpe26-256x192.py"
    model = init_model(str(config), str(checkpoint), device="cpu")
    model.eval()
    if adb is not None:
        remote = "/data/local/tmp/hv-c3-pose"
        subprocess.run([str(adb), "shell", "mkdir", "-p", remote], check=True)
        for local, name in ((runner, "runner"), (param, "model.param"), (weights, "model.bin")):
            subprocess.run([str(adb), "push", str(local), f"{remote}/{name}"], check=True)
        subprocess.run([str(adb), "shell", "chmod", "755", f"{remote}/runner"], check=True)
    source = cv2.imread(str(image_path))
    if source is None:
        raise ValueError("Missing real pose source image")
    # C2's official fixture contains the person in this detector-validated box.
    source_bbox = [18.2485520362854, 17.494831562042236, 218.0, 346.0]
    result = {}
    route_runs = []
    for name, image, bbox in cases(source, source_bbox):
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(folder / "image.png"), image)
        box = np.asarray([bbox], dtype=np.float32)
        center, scale = bbox_xyxy2cs(box, padding=1.25)
        scale = TopdownAffine._fix_aspect_ratio(scale, 192 / 256)
        warp = get_warp_matrix(center[0], scale[0], 0, (192, 256))
        inverse = cv2.invertAffineTransform(warp)
        crop = cv2.warpAffine(image, warp, (192, 256), flags=cv2.INTER_LINEAR)
        rgb = cv2.cvtColor(crop, cv2.COLOR_BGR2RGB).astype(np.float32)
        mean = np.array([123.675, 116.28, 103.53], dtype=np.float32)
        std = np.array([58.395, 57.12, 57.375], dtype=np.float32)
        tensor = np.transpose((rgb - mean) / std, (2, 0, 1))[None].copy()
        tensor.tofile(folder / "input.fp32")
        with torch.no_grad():
            raw_reference = model.head.forward(model.extract_feat(torch.from_numpy(tensor)))
        ref_x, ref_y = [part.detach().numpy() for part in raw_reference]
        for label, array in (("pytorch-x", ref_x), ("pytorch-y", ref_y)):
            array.astype(np.float32).tofile(folder / f"{label}.fp32")
        if adb is None:
            for prefix in ("ncnn", "repeat"):
                command = [str(runner), str(param), str(weights), str(folder / "input.fp32"),
                           str(folder / f"{prefix}-x.fp32"), str(folder / f"{prefix}-y.fp32")]
                subprocess.run(command + [runner_mode], check=True)
        else:
            subprocess.run([str(adb), "push", str(folder / "input.fp32"), f"{remote}/input.fp32"], check=True)
            for prefix in ("ncnn", "repeat"):
                command = [str(adb), "shell", f"{remote}/runner", f"{remote}/model.param",
                           f"{remote}/model.bin", f"{remote}/input.fp32",
                           f"{remote}/x.fp32", f"{remote}/y.fp32"]
                completed=subprocess.run(command + [runner_mode],capture_output=True,text=True)
                log=completed.stdout+completed.stderr
                print(log,flush=True)
                log_path=output/'route-logs'/f'{name}-{prefix}.log'
                log_path.parent.mkdir(parents=True,exist_ok=True)
                log_path.write_text(log,encoding='utf-8',newline='\n')
                completed.check_returncode()
                audit=parse_vkmat_audit(log)
                for axis in ("x", "y"):
                    subprocess.run([str(adb), "pull", f"{remote}/{axis}.fp32",
                                    str(folder / f"{prefix}-{axis}.fp32")], check=True)
                route_runs.append({'case':name,'prefix':prefix,'audit':audit,'log_sha256':sha256_file(log_path),
                    'input_sha256':sha256_file(folder/'input.fp32'),'x_sha256':sha256_file(folder/f'{prefix}-x.fp32'),
                    'y_sha256':sha256_file(folder/f'{prefix}-y.fp32')})
        candidate_x = np.fromfile(folder / "ncnn-x.fp32", np.float32).reshape(1, 26, 384)
        candidate_y = np.fromfile(folder / "ncnn-y.fp32", np.float32).reshape(1, 26, 512)
        repeat_x = np.fromfile(folder / "repeat-x.fp32", np.float32).reshape(1, 26, 384)
        repeat_y = np.fromfile(folder / "repeat-y.fp32", np.float32).reshape(1, 26, 512)
        reference = decode(model.head, ref_x, ref_y, inverse, bbox)
        candidate = decode(model.head, candidate_x, candidate_y, inverse, bbox)
        repeat = decode(model.head, repeat_x, repeat_y, inverse, bbox)
        import math
        mask=np.asarray(reference["valid"])
        distances=np.linalg.norm(np.asarray(reference["points"])[mask]-np.asarray(candidate["points"])[mask],axis=1)/math.hypot(bbox[2]-bbox[0],bbox[3]-bbox[1])
        metrics={"mask_equal":reference["valid"]==candidate["valid"], "reference_valid":sum(reference["valid"]), "candidate_valid":sum(candidate["valid"]), "distance_p95":float(np.percentile(distances,95)), "distance_max":float(distances.max()), "confidence_p95":float(np.percentile(np.abs(np.asarray(reference["scores"])[mask]-np.asarray(candidate["scores"])[mask]),95)), "x_p95_abs":float(np.percentile(np.abs(ref_x-candidate_x),95)), "y_p95_abs":float(np.percentile(np.abs(ref_y-candidate_y),95)), "x_argmax":int((ref_x.argmax(2)==candidate_x.argmax(2)).sum()), "y_argmax":int((ref_y.argmax(2)==candidate_y.argmax(2)).sum()), "repeat_identical":bool(np.array_equal(candidate_x,repeat_x) and np.array_equal(candidate_y,repeat_y))}
        try:
            metrics["strict"] = compare_pose(reference, candidate, bbox, (image.shape[1], image.shape[0]), repeat)
            metrics["pass"] = True
        except ValueError as error:
            metrics["error"] = str(error)
            metrics["pass"] = False
        print(name, json.dumps(metrics), flush=True)
        (folder / "crop.json").write_text(canonical_json({"bbox":bbox,"warp":warp.tolist(),"inverse_affine":inverse.tolist(),"input_shape":list(tensor.shape)}), encoding="utf-8")
        for label, value in (("reference", reference), ("candidate", candidate), ("repeat", repeat)):
            (folder / f"{label}.json").write_text(canonical_json(value), encoding="utf-8", newline="\n")
        result[name] = {"metrics": metrics, "files": {file.name: sha256_file(file)
                         for file in folder.iterdir() if file.is_file()}}
    output.mkdir(parents=True, exist_ok=True)
    (output / "index.json").write_text(canonical_json(result), encoding="utf-8", newline="\n")
    route={'schema_version':1,'mode':'strict-vkmat','runner_sha256':POSE_RUNNER_SHA256,
           'runner_source_sha256':POSE_RUNNER_SOURCE_SHA256,
           'model_sha256':{'onnx':POSE_ONNX_SHA256,'param':POSE_PARAM_SHA256,'bin':POSE_BIN_SHA256,'checkpoint':POSE_CHECKPOINT_SHA256},
           'golden_sha256':sha256_file(output/'index.json'),'runs':route_runs}
    (output/'vkmat-route.json').write_text(canonical_json(route),encoding='utf-8',newline='\n')
    if not all(case["metrics"]["pass"] for case in result.values()):
        raise SystemExit(11)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    for name in ("checkpoint", "vendor-root", "image", "onnx", "param", "weights", "runner", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--runner-mode", choices=['strict-vkmat'],required=True)
    args = parser.parse_args()
    print(canonical_json(run(args.checkpoint, args.vendor_root, args.image, args.onnx,
                             args.param, args.weights, args.runner, args.output, args.adb,
                             args.runner_mode)))


if __name__ == "__main__":
    main()
