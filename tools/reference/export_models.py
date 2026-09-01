from __future__ import annotations

import argparse
import shutil
from pathlib import Path
from typing import Any

import onnx
import onnxruntime as ort

from tools.reference.common import (
    DETECTOR_CHECKPOINT,
    DETECTOR_CHECKPOINT_URL,
    DETECTOR_CONFIG,
    DETECTOR_DEPLOY_CONFIG,
    DETECTOR_ONNX,
    MMDEPLOY_DIR,
    MMDET_DIR,
    MMPOSE_DIR,
    POSE_CHECKPOINT,
    POSE_CHECKPOINT_URL,
    POSE_CONFIG,
    POSE_DEPLOY_CONFIG,
    POSE_ONNX,
    REFERENCE_IMAGE,
    WORK_DIR,
    package_versions,
    relative_path,
    require_locked_reference_assets,
    write_json,
)
from tools.reference.contracts import sha256_file, validate_model_info


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export the locked D0.1 models with MMDeploy.")
    parser.add_argument("--model", choices=("all", "detector", "pose"), default="all")
    parser.add_argument("--device", default="cpu")
    return parser.parse_args()


def _dtype_name(ort_type: str) -> str:
    return {
        "tensor(float)": "float32",
        "tensor(double)": "float64",
        "tensor(int64)": "int64",
        "tensor(int32)": "int32",
    }.get(ort_type, ort_type)


def _tensor_descriptors(values: list[Any]) -> list[dict[str, Any]]:
    return [
        {"dtype": _dtype_name(value.type), "name": value.name, "shape": value.shape}
        for value in values
    ]


def _inspect_onnx(model_path: Path) -> tuple[int, list[dict], list[dict]]:
    model = onnx.load(str(model_path))
    onnx.checker.check_model(model)
    opset = max(item.version for item in model.opset_import if item.domain in ("", "ai.onnx"))
    session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    return opset, _tensor_descriptors(session.get_inputs()), _tensor_descriptors(session.get_outputs())


def _export(
    deploy_config: Path,
    model_config: Path,
    checkpoint: Path,
    work_dir: Path,
    destination: Path,
    device: str,
) -> None:
    from mmdeploy.apis import torch2onnx

    work_dir.mkdir(parents=True, exist_ok=True)
    torch2onnx(
        str(REFERENCE_IMAGE),
        str(work_dir),
        "end2end.onnx",
        str(deploy_config),
        str(model_config),
        str(checkpoint),
        device,
    )
    exported = work_dir / "end2end.onnx"
    if not exported.is_file():
        raise RuntimeError(f"MMDeploy did not produce the expected ONNX file: {exported}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exported, destination)


def _source_metadata(
    framework: str,
    framework_version: str,
    config_identifier: str,
    config_path: Path,
    checkpoint: Path,
    checkpoint_url: str,
    source_tag: str,
    source_commit: str,
) -> dict:
    return {
        "checkpoint_filename": checkpoint.name,
        "checkpoint_sha256": sha256_file(checkpoint),
        "checkpoint_url": checkpoint_url,
        "config_identifier": config_identifier,
        "config_path": relative_path(config_path),
        "framework": framework,
        "framework_version": framework_version,
        "source_commit": source_commit,
        "source_tag": source_tag,
    }


def _write_model_info(model_path: Path, info: dict) -> None:
    errors = validate_model_info(info, model_path)
    if errors:
        raise RuntimeError("Invalid model_info contract:\n  - " + "\n  - ".join(errors))
    write_json(model_path.parent / "model_info.json", info)


def export_detector(device: str) -> None:
    destination = DETECTOR_ONNX
    _export(
        DETECTOR_DEPLOY_CONFIG,
        DETECTOR_CONFIG,
        DETECTOR_CHECKPOINT,
        WORK_DIR / "detector",
        destination,
        device,
    )
    opset, inputs, outputs = _inspect_onnx(destination)
    info = {
        "schema_version": 1,
        "model_role": "detector",
        "family": "RTMDet",
        "source": _source_metadata(
            "MMDetection",
            "3.2.0",
            "rtmdet_tiny_8xb32-300e_coco",
            DETECTOR_CONFIG,
            DETECTOR_CHECKPOINT,
            DETECTOR_CHECKPOINT_URL,
            "v3.2.0",
            "fe3f809a0a514189baf889aa358c498d51ee36cd",
        ),
        "export": {
            "api": "mmdeploy.apis.torch2onnx",
            "command": ["python", "-m", "tools.reference.export_models", "--model", "detector"],
            "deploy_config": relative_path(DETECTOR_DEPLOY_CONFIG),
            "opset": opset,
            "tool": "MMDeploy",
            "tool_source_commit": "bc75c9d6c8940aa03d0e1e5b5962bd930478ba77",
            "tool_source_tag": "v1.3.1",
            "tool_version": "1.3.1",
        },
        "onnx": {
            "filename": destination.name,
            "inputs": inputs,
            "outputs": outputs,
            "sha256": sha256_file(destination),
        },
        "preprocessing": {
            "bgr_to_rgb": False,
            "color_order": "BGR",
            "input_size_width_height": [640, 640],
            "keep_aspect_ratio": True,
            "mean": [103.53, 116.28, 123.675],
            "pad_value_bgr": [114, 114, 114],
            "std": [57.375, 57.12, 58.395],
        },
        "postprocessing": {
            "export_iou_threshold": 0.5,
            "export_keep_top_k": 100,
            "export_score_threshold": 0.05,
            "label_tensor_encoding": "float32 class ids; round then cast to integer",
            "onnx_bbox_coordinate_space": "resized/padded 640x640 detector input pixels",
            "person_class_id": 0,
            "restore_to_source": "divide x coordinates by scale_factor_x and y coordinates by scale_factor_y",
            "runtime_detection_threshold": 0.35,
            "runtime_max_bodies": "config.max_bodies",
        },
        "reference_image": {
            "path": relative_path(REFERENCE_IMAGE),
            "sha256": sha256_file(REFERENCE_IMAGE),
        },
        "environment": package_versions(),
    }
    _write_model_info(destination, info)


def export_pose(device: str) -> None:
    destination = POSE_ONNX
    _export(
        POSE_DEPLOY_CONFIG,
        POSE_CONFIG,
        POSE_CHECKPOINT,
        WORK_DIR / "pose",
        destination,
        device,
    )
    opset, inputs, outputs = _inspect_onnx(destination)
    info = {
        "schema_version": 1,
        "model_role": "pose",
        "family": "RTMPose",
        "source": _source_metadata(
            "MMPose",
            "1.3.2",
            "rtmpose-s_8xb256-420e_coco-256x192",
            POSE_CONFIG,
            POSE_CHECKPOINT,
            POSE_CHECKPOINT_URL,
            "v1.3.2",
            "5408bc76f5b848cf925a0d1857899011d8c5b497",
        ),
        "export": {
            "api": "mmdeploy.apis.torch2onnx",
            "command": ["python", "-m", "tools.reference.export_models", "--model", "pose"],
            "deploy_config": relative_path(POSE_DEPLOY_CONFIG),
            "opset": opset,
            "tool": "MMDeploy",
            "tool_source_commit": "bc75c9d6c8940aa03d0e1e5b5962bd930478ba77",
            "tool_source_tag": "v1.3.1",
            "tool_version": "1.3.1",
        },
        "onnx": {
            "filename": destination.name,
            "inputs": inputs,
            "outputs": outputs,
            "sha256": sha256_file(destination),
        },
        "preprocessing": {
            "affine": "MMPose TopdownAffine on the selected person ROI",
            "affine_interpolation": "bilinear",
            "bbox_format": "xyxy",
            "bbox_padding_factor": 1.25,
            "fixed_aspect_ratio_width_height": [192, 256],
            "bgr_to_rgb": True,
            "input_size_width_height": [192, 256],
            "mean": [123.675, 116.28, 103.53],
            "std": [58.395, 57.12, 57.375],
            "use_udp": False,
        },
        "postprocessing": {
            "decoded_coordinate_space": "source image pixels after ROI restoration",
            "decoder_coordinate_rule": "argmax each axis, divide by simcc_split_ratio",
            "decoder": "SimCC",
            "joint_schema": "COCO-17",
            "onnx_output_space": "unnormalized SimCC logits: x=384 bins, y=512 bins",
            "roi_restoration_rule": "decoded_xy / input_size * padded_roi_scale + roi_center - 0.5 * padded_roi_scale",
            "score_rule": "minimum of the x-axis and y-axis maximum logits",
            "simcc_split_ratio": 2.0,
            "deployment_flip_test": False,
            "runtime_pose_threshold": 0.30,
        },
        "reference_image": {
            "path": relative_path(REFERENCE_IMAGE),
            "sha256": sha256_file(REFERENCE_IMAGE),
        },
        "environment": package_versions(),
    }
    _write_model_info(destination, info)


def main() -> int:
    args = parse_args()
    require_locked_reference_assets()
    if args.model in ("all", "detector"):
        export_detector(args.device)
        print(f"Exported detector: {DETECTOR_ONNX}")
    if args.model in ("all", "pose"):
        export_pose(args.device)
        print(f"Exported pose: {POSE_ONNX}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
