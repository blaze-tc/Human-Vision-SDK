from __future__ import annotations

import argparse
import gc
from pathlib import Path

import cv2
import numpy as np

from tools.reference.common import (
    COCO_17_NAMES,
    DETECTOR_CHECKPOINT,
    DETECTOR_CHECKPOINT_URL,
    DETECTOR_CONFIG,
    GOLDEN_DIR,
    POSE_CHECKPOINT,
    POSE_CHECKPOINT_URL,
    POSE_CONFIG,
    REFERENCE_IMAGE,
    package_versions,
    relative_path,
    require_locked_reference_assets,
    write_json,
)
from tools.reference.contracts import sha256_file


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run official D0.1 PyTorch reference inference.")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--output-dir", type=Path, default=GOLDEN_DIR)
    parser.add_argument("--detection-threshold", type=float, default=0.35)
    return parser.parse_args()


def run_detector(device: str, threshold: float) -> tuple[dict, list[float]]:
    from mmdet.apis import inference_detector, init_detector

    model = init_detector(str(DETECTOR_CONFIG), str(DETECTOR_CHECKPOINT), device=device)
    result = inference_detector(model, str(REFERENCE_IMAGE))
    predictions = result.pred_instances.cpu().numpy()

    people = []
    for bbox, score, label in zip(
        predictions.bboxes, predictions.scores, predictions.labels, strict=True
    ):
        if int(label) != 0 or float(score) < threshold:
            continue
        people.append(
            {
                "bbox_xyxy": [float(value) for value in bbox],
                "class_id": 0,
                "class_name": "person",
                "score": float(score),
            }
        )
    people.sort(key=lambda item: item["score"], reverse=True)
    if not people:
        raise RuntimeError("Official RTMDet reference produced no person above the threshold.")

    image = cv2.imread(str(REFERENCE_IMAGE), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"Could not read reference image: {REFERENCE_IMAGE}")
    height, width = image.shape[:2]

    payload = {
        "schema_version": 1,
        "kind": "pytorch_reference",
        "model_role": "detector",
        "model_family": "RTMDet",
        "config_identifier": "rtmdet_tiny_8xb32-300e_coco",
        "config_path": relative_path(DETECTOR_CONFIG),
        "checkpoint": {
            "filename": DETECTOR_CHECKPOINT.name,
            "sha256": sha256_file(DETECTOR_CHECKPOINT),
            "url": DETECTOR_CHECKPOINT_URL,
        },
        "input": {
            "image_path": relative_path(REFERENCE_IMAGE),
            "image_sha256": sha256_file(REFERENCE_IMAGE),
            "width": width,
            "height": height,
        },
        "runtime": {"device": device, "detection_threshold": threshold},
        "environment": package_versions(),
        "predictions": people,
    }

    del model
    gc.collect()
    return payload, people[0]["bbox_xyxy"]


def run_pose(device: str, bbox_xyxy: list[float]) -> dict:
    from mmpose.apis import inference_topdown, init_model

    model = init_model(str(POSE_CONFIG), str(POSE_CHECKPOINT), device=device)
    model.test_cfg["flip_test"] = False
    bboxes = np.asarray([bbox_xyxy], dtype=np.float32)
    results = inference_topdown(model, str(REFERENCE_IMAGE), bboxes=bboxes, bbox_format="xyxy")
    if len(results) != 1:
        raise RuntimeError(f"Expected one RTMPose result, received {len(results)}.")

    predictions = results[0].pred_instances
    keypoints = predictions.keypoints[0]
    scores = predictions.keypoint_scores[0]
    joints = [
        {
            "confidence": float(score),
            "index": index,
            "name": COCO_17_NAMES[index],
            "x_px": float(point[0]),
            "y_px": float(point[1]),
        }
        for index, (point, score) in enumerate(zip(keypoints, scores, strict=True))
    ]
    if len(joints) != 17:
        raise RuntimeError(f"Expected COCO-17 output, received {len(joints)} joints.")

    payload = {
        "schema_version": 1,
        "kind": "pytorch_reference",
        "model_role": "pose",
        "model_family": "RTMPose",
        "config_identifier": "rtmpose-s_8xb256-420e_coco-256x192",
        "config_path": relative_path(POSE_CONFIG),
        "checkpoint": {
            "filename": POSE_CHECKPOINT.name,
            "sha256": sha256_file(POSE_CHECKPOINT),
            "url": POSE_CHECKPOINT_URL,
        },
        "input": {
            "image_path": relative_path(REFERENCE_IMAGE),
            "image_sha256": sha256_file(REFERENCE_IMAGE),
            "person_bbox_xyxy": bbox_xyxy,
        },
        "runtime": {"device": device, "flip_test": False},
        "environment": package_versions(),
        "joints": joints,
    }

    del model
    gc.collect()
    return payload


def main() -> int:
    args = parse_args()
    require_locked_reference_assets()
    detector, person_bbox = run_detector(args.device, args.detection_threshold)
    pose = run_pose(args.device, person_bbox)
    write_json(args.output_dir / "detector_reference.json", detector)
    write_json(args.output_dir / "pose_reference.json", pose)
    print(
        f"Wrote official PyTorch references: {len(detector['predictions'])} person(s), "
        f"{len(pose['joints'])} joints"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
