from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

import numpy as np
import onnxruntime as ort
import torch

from tools.reference.common import (
    DETECTOR_CHECKPOINT,
    DETECTOR_CONFIG,
    DETECTOR_DEPLOY_CONFIG,
    DETECTOR_ONNX,
    GOLDEN_DIR,
    POSE_CHECKPOINT,
    POSE_CONFIG,
    POSE_ONNX,
    REFERENCE_IMAGE,
    require_paths,
    write_json,
)
from tools.reference.contracts import sha256_file, validate_model_info


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _iou(a: list[float], b: list[float]) -> float:
    x1 = max(a[0], b[0])
    y1 = max(a[1], b[1])
    x2 = min(a[2], b[2])
    y2 = min(a[3], b[3])
    intersection = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    area_a = max(0.0, a[2] - a[0]) * max(0.0, a[3] - a[1])
    area_b = max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])
    union = area_a + area_b - intersection
    return intersection / union if union > 0.0 else 0.0


def _restore_detector_bbox(
    bbox_xyxy: np.ndarray, scale_factor_xy: tuple[float, float]
) -> list[float]:
    """Map MMDeploy detector output from resized input pixels to source pixels."""
    scale_x, scale_y = scale_factor_xy
    restored = bbox_xyxy.astype(np.float64).copy()
    restored[[0, 2]] /= scale_x
    restored[[1, 3]] /= scale_y
    return restored.tolist()


def compare_detector(reference: dict) -> dict[str, Any]:
    from mmdeploy.apis import build_task_processor
    from mmdeploy.utils import get_input_shape, load_config

    deploy_cfg, model_cfg = load_config(str(DETECTOR_DEPLOY_CONFIG), str(DETECTOR_CONFIG))
    task_processor = build_task_processor(model_cfg, deploy_cfg, "cpu")
    pytorch_model = task_processor.build_pytorch_model(str(DETECTOR_CHECKPOINT))
    data, model_inputs = task_processor.create_input(
        str(REFERENCE_IMAGE),
        get_input_shape(deploy_cfg),
        data_preprocessor=getattr(pytorch_model, "data_preprocessor", None),
    )
    if isinstance(model_inputs, list):
        model_inputs = model_inputs[0]
    input_array = model_inputs.detach().cpu().numpy()
    scale_factor = tuple(float(value) for value in data["data_samples"][0].scale_factor)

    session = ort.InferenceSession(str(DETECTOR_ONNX), providers=["CPUExecutionProvider"])
    outputs = session.run(None, {session.get_inputs()[0].name: input_array})
    dets = outputs[0][0]
    labels = outputs[1][0]
    candidates = [
        {
            "bbox_xyxy": _restore_detector_bbox(row[:4], scale_factor),
            "score": float(row[4]),
        }
        for row, label in zip(dets, labels, strict=True)
        if int(round(float(label))) == 0 and float(row[4]) >= 0.35
    ]
    candidates.sort(key=lambda item: item["score"], reverse=True)
    expected = reference["predictions"]

    matched = []
    available = candidates.copy()
    for item in expected:
        if not available:
            break
        best_index = max(
            range(len(available)),
            key=lambda index: _iou(item["bbox_xyxy"], available[index]["bbox_xyxy"]),
        )
        candidate = available.pop(best_index)
        expected_bbox = np.asarray(item["bbox_xyxy"], dtype=np.float64)
        actual_bbox = np.asarray(candidate["bbox_xyxy"], dtype=np.float64)
        matched.append(
            {
                "bbox_max_abs_px": float(np.max(np.abs(expected_bbox - actual_bbox))),
                "iou": _iou(item["bbox_xyxy"], candidate["bbox_xyxy"]),
                "score_abs": abs(float(item["score"]) - candidate["score"]),
            }
        )

    tolerances = {"bbox_max_abs_px": 1.5, "iou_min": 0.99, "score_abs": 0.01}
    passed = len(expected) == len(candidates) == len(matched) and all(
        item["bbox_max_abs_px"] <= tolerances["bbox_max_abs_px"]
        and item["iou"] >= tolerances["iou_min"]
        and item["score_abs"] <= tolerances["score_abs"]
        for item in matched
    )
    del pytorch_model
    return {
        "pass": passed,
        "expected_count": len(expected),
        "onnx_count": len(candidates),
        "matches": matched,
        "coordinate_restoration": {
            "onnx_space": "resized/padded detector input pixels",
            "source_space": "original image pixels",
            "scale_factor_xy": list(scale_factor),
        },
        "tolerances": tolerances,
    }


def _pose_batch(model: Any, bbox_xyxy: list[float]) -> dict:
    from mmcv.transforms import Compose
    from mmengine.dataset import pseudo_collate
    from mmengine.registry import init_default_scope

    init_default_scope(model.cfg.get("default_scope", "mmpose"))
    pipeline = Compose(model.cfg.test_dataloader.dataset.pipeline)
    data_info = {
        "bbox": np.asarray(bbox_xyxy, dtype=np.float32)[None],
        "bbox_score": np.ones(1, dtype=np.float32),
        "img_path": str(REFERENCE_IMAGE),
    }
    data_info.update(model.dataset_meta)
    batch = pseudo_collate([pipeline(data_info)])
    return model.data_preprocessor(batch, training=False)


def _decode_pose(model: Any, outputs: tuple[torch.Tensor, torch.Tensor], data_samples: list) -> tuple[np.ndarray, np.ndarray]:
    instances = model.head.decode(outputs)
    samples = model.add_pred_to_datasample(instances, None, copy.deepcopy(data_samples))
    predictions = samples[0].pred_instances
    return predictions.keypoints[0], predictions.keypoint_scores[0]


def compare_pose(reference: dict) -> dict[str, Any]:
    from mmpose.apis import init_model

    model = init_model(str(POSE_CONFIG), str(POSE_CHECKPOINT), device="cpu")
    model.test_cfg["flip_test"] = False
    processed = _pose_batch(model, reference["input"]["person_bbox_xyxy"])
    input_tensor = processed["inputs"]

    with torch.no_grad():
        features = model.extract_feat(input_tensor)
        pytorch_outputs = model.head.forward(features)

    session = ort.InferenceSession(str(POSE_ONNX), providers=["CPUExecutionProvider"])
    onnx_arrays = session.run(
        None, {session.get_inputs()[0].name: input_tensor.detach().cpu().numpy()}
    )
    onnx_outputs = tuple(torch.from_numpy(value) for value in onnx_arrays)

    raw_metrics = []
    for pytorch_value, onnx_value in zip(pytorch_outputs, onnx_outputs, strict=True):
        difference = np.abs(pytorch_value.detach().cpu().numpy() - onnx_value.numpy())
        raw_metrics.append(
            {"max_abs": float(difference.max()), "mean_abs": float(difference.mean())}
        )

    onnx_keypoints, onnx_scores = _decode_pose(
        model, onnx_outputs, processed["data_samples"]
    )
    expected_keypoints = np.asarray(
        [[joint["x_px"], joint["y_px"]] for joint in reference["joints"]],
        dtype=np.float64,
    )
    expected_scores = np.asarray(
        [joint["confidence"] for joint in reference["joints"]], dtype=np.float64
    )
    coordinate_difference = np.abs(expected_keypoints - onnx_keypoints)
    score_difference = np.abs(expected_scores - onnx_scores)

    tolerances = {
        "coordinate_max_abs_px": 0.5,
        "raw_max_abs": 0.002,
        "score_max_abs": 0.005,
    }
    passed = (
        len(onnx_keypoints) == 17
        and max(item["max_abs"] for item in raw_metrics) <= tolerances["raw_max_abs"]
        and float(coordinate_difference.max()) <= tolerances["coordinate_max_abs_px"]
        and float(score_difference.max()) <= tolerances["score_max_abs"]
    )
    return {
        "pass": passed,
        "joint_count": int(len(onnx_keypoints)),
        "raw_outputs": raw_metrics,
        "coordinate_max_abs_px": float(coordinate_difference.max()),
        "coordinate_mean_abs_px": float(coordinate_difference.mean()),
        "score_max_abs": float(score_difference.max()),
        "score_mean_abs": float(score_difference.mean()),
        "tolerances": tolerances,
    }


def main() -> int:
    detector_reference_path = GOLDEN_DIR / "detector_reference.json"
    pose_reference_path = GOLDEN_DIR / "pose_reference.json"
    require_paths(
        [
            DETECTOR_ONNX,
            POSE_ONNX,
            DETECTOR_ONNX.parent / "model_info.json",
            POSE_ONNX.parent / "model_info.json",
            detector_reference_path,
            pose_reference_path,
        ]
    )

    for model_path in (DETECTOR_ONNX, POSE_ONNX):
        info = _load_json(model_path.parent / "model_info.json")
        errors = validate_model_info(info, model_path)
        if errors:
            raise RuntimeError("Invalid model contract:\n  - " + "\n  - ".join(errors))

    detector = compare_detector(_load_json(detector_reference_path))
    pose = compare_pose(_load_json(pose_reference_path))
    summary = {
        "schema_version": 1,
        "pass": bool(detector["pass"] and pose["pass"]),
        "detector": detector,
        "pose": pose,
        "models": {
            "detector_sha256": sha256_file(DETECTOR_ONNX),
            "pose_sha256": sha256_file(POSE_ONNX),
        },
    }
    write_json(GOLDEN_DIR / "comparison_summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
