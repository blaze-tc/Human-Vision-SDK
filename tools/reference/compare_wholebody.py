"""S1 official-reference comparison and explicitly labelled ROI compute probe."""

from __future__ import annotations

import argparse
import copy
import json
import time
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort
import torch

from tools.reference.common import (
    GOLDEN_DIR, PROJECT_ROOT, REFERENCE_IMAGE, package_versions,
    require_locked_reference_assets, write_json,
)
from tools.reference.contracts import sha256_file
from tools.reference.wholebody_contract import semantic_endpoints, validate_simcc


def make_batch(model, image_path: Path, bbox: list[float]):
    from mmcv.transforms import Compose
    from mmengine.dataset import pseudo_collate
    from mmengine.registry import init_default_scope

    init_default_scope(model.cfg.get("default_scope", "mmpose"))
    info = {"bbox": np.asarray([bbox], np.float32), "bbox_score": np.ones(1, np.float32),
            "img_path": str(image_path)}
    info.update(model.dataset_meta)
    pipeline = Compose(model.cfg.test_dataloader.dataset.pipeline)
    return model.data_preprocessor(pseudo_collate([pipeline(info)]), training=False)


def decode(model, outputs, samples):
    instances = model.head.decode(tuple(torch.from_numpy(value) for value in outputs))
    result = model.add_pred_to_datasample(instances, None, copy.deepcopy(samples))[0].pred_instances
    return result.keypoints[0], result.keypoint_scores[0]


def timing_summary(durations):
    return {"mean_ms": float(np.mean(durations)), "p50_ms": float(np.percentile(durations, 50)),
            "p95_ms": float(np.percentile(durations, 95)), "max_ms": float(max(durations)),
            "model_only_cycles_per_second": 1000.0 / float(np.mean(durations))}


def draw_evidence(image_path, points, scores, endpoints, output_path):
    image = cv2.imread(str(image_path))
    if image is None:
        raise ValueError(f"Could not read {image_path}")
    scale = max(1.0, 900.0 / image.shape[0])
    canvas = cv2.resize(image, None, fx=scale, fy=scale)
    body_edges = [(5, 6), (5, 7), (7, 9), (6, 8), (8, 10), (5, 11),
                  (6, 12), (11, 12), (11, 13), (13, 15), (12, 14), (14, 16)]
    for root in (91, 112):
        for start in (1, 5, 9, 13, 17):
            chain = [root, root + start, root + start + 1, root + start + 2, root + start + 3]
            body_edges.extend(zip(chain[:-1], chain[1:]))
    for a, b in body_edges:
        if min(scores[a], scores[b]) >= 0.3:
            cv2.line(canvas, tuple(np.rint(points[a] * scale).astype(int)),
                     tuple(np.rint(points[b] * scale).astype(int)), (0, 220, 220), 2)
    for name, sample in endpoints.items():
        if sample["valid"]:
            p = tuple(np.rint(np.asarray(sample["pixel"]) * scale).astype(int))
            cv2.circle(canvas, p, 5, (255, 0, 255), -1)
            cv2.putText(canvas, name, p, cv2.FONT_HERSHEY_SIMPLEX, 0.35, (255, 0, 255), 1)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(output_path), canvas):
        raise RuntimeError(f"Cannot save evidence image: {output_path}")


def run_candidate(item, args):
    from mmpose.apis import init_model

    for field, hash_field in (("config_path", "config_sha256"),
                             ("checkpoint_path", "checkpoint_sha256"),
                             ("onnx_path", "onnx_sha256")):
        if sha256_file(PROJECT_ROOT / item[field]) != item[hash_field]:
            raise ValueError(f"Pinned candidate hash mismatch: {field}")
    model = init_model(str(PROJECT_ROOT / item["config_path"]),
                       str(PROJECT_ROOT / item["checkpoint_path"]), device="cpu")
    model.test_cfg["flip_test"] = False
    expected_names = {95: "left_thumb4", 103: "left_middle_finger4",
                      116: "right_thumb4", 124: "right_middle_finger4"}
    actual_names = model.dataset_meta["keypoint_id2name"]
    for index, expected in expected_names.items():
        if actual_names[index] != expected:
            raise ValueError(f"Unexpected metadata at {index}: {actual_names[index]}")
    reference = json.loads((GOLDEN_DIR / "pose_reference.json").read_text())
    processed = make_batch(model, REFERENCE_IMAGE, reference["input"]["person_bbox_xyxy"])
    tensor = processed["inputs"]
    with torch.no_grad():
        torch_outputs = tuple(value.detach().cpu().numpy()
                              for value in model.head.forward(model.extract_feat(tensor)))
    validate_simcc(*torch_outputs)
    options = ort.SessionOptions()
    options.intra_op_num_threads = args.threads
    session = ort.InferenceSession(str(PROJECT_ROOT / item["onnx_path"]), options,
                                   providers=["CPUExecutionProvider"])
    if len(session.get_inputs()) != 1 or len(session.get_outputs()) != 2:
        raise ValueError("Expected one image input and two SimCC outputs")
    output_names = ["simcc_x", "simcc_y"]
    feed = {session.get_inputs()[0].name: tensor.detach().cpu().numpy()}
    arrays = session.run(output_names, feed)
    validate_simcc(*arrays)
    points, scores = decode(model, arrays, processed["data_samples"])
    reference_points, reference_scores = decode(model, torch_outputs, processed["data_samples"])
    raw_error = max(float(np.abs(a - b).max()) for a, b in zip(arrays, torch_outputs))
    point_error = float(np.abs(points - reference_points).max())
    score_error = float(np.abs(scores - reference_scores).max())
    agreement = raw_error <= 0.002 and point_error <= 0.5 and score_error <= 0.005
    endpoints = semantic_endpoints(points, scores, 0.3)
    workload = []
    for _ in range(args.warmup):
        session.run(output_names, feed)
    for count in (1, 2, 4, 8):
        durations = []
        for _ in range(args.repetitions):
            start = time.perf_counter()
            for _ in range(count):
                session.run(output_names, feed)
            durations.append((time.perf_counter() - start) * 1000)
        workload.append({"roi_count": count, "mode": "serial_repeat_of_one_real_roi",
                         "samples": len(durations), **timing_summary(durations)})
    # Declared dynamic batch is exercised rather than assumed to work.
    batches = []
    for count in (2, 4, 8):
        try:
            batch_feed = {session.get_inputs()[0].name: np.repeat(next(iter(feed.values())), count, axis=0)}
            batch_outputs = session.run(output_names, batch_feed)
            validate_simcc(*batch_outputs)
            error = max(float(np.abs(value - np.repeat(single, count, axis=0)).max())
                        for value, single in zip(batch_outputs, arrays))
            batches.append({"batch": count, "execution_pass": True, "max_raw_error_vs_single": error,
                            "agreement_pass": error <= 0.002})
        except (ValueError, RuntimeError, ort.capi.onnxruntime_pybind11_state.Fail,
                ort.capi.onnxruntime_pybind11_state.InvalidArgument) as error:
            batches.append({"batch": count, "execution_pass": False, "error": str(error)})
    payload = {
        "schema_version": 1, "candidate_size": item["size"], "onnx_sha256": item["onnx_sha256"],
        "reference_kind": "official_pytorch_same_preprocessed_real_image_no_flip",
        "image": str(REFERENCE_IMAGE.relative_to(PROJECT_ROOT)), "image_sha256": sha256_file(REFERENCE_IMAGE),
        "environment": package_versions(), "ort_providers": session.get_providers(),
        "ort_intra_op_threads": args.threads, "torch_threads": torch.get_num_threads(),
        "joint_count": len(points), "input_shape": list(tensor.shape),
        "output_shapes": [list(value.shape) for value in arrays],
        "agreement": {"pass": agreement, "raw_max_abs": raw_error, "coordinate_max_abs_px": point_error,
                      "score_max_abs": score_error, "limits": {"raw_max_abs": 0.002,
                      "coordinate_max_abs_px": 0.5, "score_max_abs": 0.005}},
        "keypoints": points.tolist(), "scores": scores.tolist(), "semantic_endpoints": endpoints,
        "serial_workloads": workload, "dynamic_batch_checks": batches,
        "not_measured": ["actual_eight_people_accuracy", "detector_and_preprocess_in_workload_timing",
                         "end_to_end_fps", "hand_accuracy_ground_truth", "gpu", "rk3588"],
    }
    destination = args.output_dir / f"wholebody_{item['size']}_threads{args.threads}.json"
    write_json(destination, payload)
    draw_evidence(REFERENCE_IMAGE, points, scores, endpoints, destination.with_suffix(".png"))
    print(json.dumps({"candidate": item["size"], "agreement": payload["agreement"],
                      "endpoints": endpoints, "workloads": workload, "batch": batches}), flush=True)
    return agreement


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", choices=("s", "m", "all"), default="all")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--repetitions", type=int, default=20)
    parser.add_argument("--output-dir", type=Path, default=PROJECT_ROOT / "out/validation/s1")
    args = parser.parse_args()
    if args.threads < 0 or args.warmup < 0 or args.repetitions < 1:
        parser.error("threads/warmup must be nonnegative and repetitions positive")
    require_locked_reference_assets()
    torch.set_num_threads(1)
    manifest = json.loads((PROJECT_ROOT / "models/wholebody/candidates.json").read_text())
    results = [run_candidate(item, args) for item in manifest["candidates"]
               if args.size == "all" or item["size"] == args.size]
    if not results:
        raise ValueError("No requested model candidate in manifest")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
