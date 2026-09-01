from __future__ import annotations

import importlib.metadata
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[2]
REFERENCE_DIR = PROJECT_ROOT / "tools" / "reference"
VENDOR_DIR = REFERENCE_DIR / "vendor"
DOWNLOAD_DIR = REFERENCE_DIR / "downloads"
WORK_DIR = REFERENCE_DIR / "work"

MMDET_DIR = VENDOR_DIR / "mmdetection"
MMPOSE_DIR = VENDOR_DIR / "mmpose"
MMDEPLOY_DIR = VENDOR_DIR / "mmdeploy"
MMDET_COMMIT = "fe3f809a0a514189baf889aa358c498d51ee36cd"
MMPOSE_COMMIT = "5408bc76f5b848cf925a0d1857899011d8c5b497"
MMDEPLOY_COMMIT = "bc75c9d6c8940aa03d0e1e5b5962bd930478ba77"

DETECTOR_CONFIG = MMDET_DIR / "configs" / "rtmdet" / "rtmdet_tiny_8xb32-300e_coco.py"
DETECTOR_CHECKPOINT = DOWNLOAD_DIR / "rtmdet_tiny_8xb32-300e_coco_20220902_112414-78e30dcc.pth"
DETECTOR_CHECKPOINT_URL = (
    "https://download.openmmlab.com/mmdetection/v3.0/rtmdet/"
    "rtmdet_tiny_8xb32-300e_coco/"
    "rtmdet_tiny_8xb32-300e_coco_20220902_112414-78e30dcc.pth"
)
DETECTOR_CHECKPOINT_SHA256 = "78e30dcce0c6f594eaff0d6977b84b4103688b4aff0ad1aa16008a8cc854a7fb"
DETECTOR_DEPLOY_CONFIG = (
    MMDEPLOY_DIR / "configs" / "mmdet" / "detection" / "detection_onnxruntime_static.py"
)

POSE_CONFIG = (
    MMPOSE_DIR
    / "configs"
    / "body_2d_keypoint"
    / "rtmpose"
    / "coco"
    / "rtmpose-s_8xb256-420e_coco-256x192.py"
)
POSE_CHECKPOINT = DOWNLOAD_DIR / "rtmpose-s_simcc-coco_pt-aic-coco_420e-256x192-8edcf0d7_20230127.pth"
POSE_CHECKPOINT_URL = (
    "https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/"
    "rtmpose-s_simcc-coco_pt-aic-coco_420e-256x192-8edcf0d7_20230127.pth"
)
POSE_CHECKPOINT_SHA256 = "29dcacbb5c5f3ab2f03a67fedcb58cf7287f93b9a8a9d893f42416b63fc304ba"
POSE_DEPLOY_CONFIG = (
    MMDEPLOY_DIR / "configs" / "mmpose" / "pose-detection_simcc_onnxruntime_dynamic.py"
)

REFERENCE_IMAGE = MMDEPLOY_DIR / "demo" / "resources" / "human-pose.jpg"
REFERENCE_IMAGE_SHA256 = "dd25fd8186e9ce27625520e24ac13ec6747316e51d20b124d3271c5764686d4e"
GOLDEN_DIR = PROJECT_ROOT / "tests" / "golden" / "d0_1"

DETECTOR_ONNX = PROJECT_ROOT / "models" / "detector" / "rtmdet_tiny_640.onnx"
POSE_ONNX = PROJECT_ROOT / "models" / "pose" / "rtmpose_s_256x192.onnx"

COCO_17_NAMES = (
    "Nose",
    "LeftEye",
    "RightEye",
    "LeftEar",
    "RightEar",
    "LeftShoulder",
    "RightShoulder",
    "LeftElbow",
    "RightElbow",
    "LeftWrist",
    "RightWrist",
    "LeftHip",
    "RightHip",
    "LeftKnee",
    "RightKnee",
    "LeftAnkle",
    "RightAnkle",
)


def require_paths(paths: list[Path]) -> None:
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        joined = "\n  - ".join(missing)
        raise FileNotFoundError(
            f"Required D0.1 assets are missing:\n  - {joined}\n"
            "Follow tools/reference/README.md to prepare official sources and checkpoints."
        )


def require_locked_reference_assets() -> None:
    from tools.reference.contracts import sha256_file

    required_files = [
        DETECTOR_CONFIG,
        DETECTOR_CHECKPOINT,
        POSE_CONFIG,
        POSE_CHECKPOINT,
        DETECTOR_DEPLOY_CONFIG,
        POSE_DEPLOY_CONFIG,
        REFERENCE_IMAGE,
    ]
    require_paths(required_files)

    expected_hashes = {
        DETECTOR_CHECKPOINT: DETECTOR_CHECKPOINT_SHA256,
        POSE_CHECKPOINT: POSE_CHECKPOINT_SHA256,
        REFERENCE_IMAGE: REFERENCE_IMAGE_SHA256,
    }
    for path, expected in expected_hashes.items():
        actual = sha256_file(path)
        if actual != expected:
            raise RuntimeError(
                f"Locked reference asset hash mismatch for {path}: "
                f"expected {expected}, received {actual}"
            )

    expected_commits = {
        MMDET_DIR: MMDET_COMMIT,
        MMPOSE_DIR: MMPOSE_COMMIT,
        MMDEPLOY_DIR: MMDEPLOY_COMMIT,
    }
    for repository, expected in expected_commits.items():
        result = subprocess.run(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
        actual = result.stdout.strip()
        if actual != expected:
            raise RuntimeError(
                f"Locked source commit mismatch for {repository}: "
                f"expected {expected}, received {actual}"
            )


def relative_path(path: Path) -> str:
    return path.resolve().relative_to(PROJECT_ROOT.resolve()).as_posix()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def package_versions() -> dict[str, str]:
    packages = (
        "torch",
        "torchvision",
        "numpy",
        "onnx",
        "onnxruntime",
        "mmengine",
        "mmcv",
        "mmdet",
        "mmpose",
        "mmdeploy",
    )
    versions = {name: importlib.metadata.version(name) for name in packages}
    versions["python"] = sys.version.split()[0]
    return versions
