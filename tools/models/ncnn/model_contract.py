"""Pinned, offline contracts for C1 conversion artifacts. No runtime fallback lives here."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Mapping

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))


DETECTOR_CHECKPOINT_SHA256 = "05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1"
DETECTOR_CHECKPOINT_URL = ("https://download.openmmlab.com/mmpose/v1/projects/rtmpose/"
                           "rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth")
POSE_ARCHIVE_URL = ("https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/"
                    "rtmpose-t_simcc-body7_pt-body7-halpe26_700e-256x192-6020f8a6_20230605.zip")
POSE_CHECKPOINT_URL = ("https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/"
                       "rtmpose-t_simcc-body7_pt-body7-halpe26_700e-256x192-6020f8a6_20230605.pth")
POSE_CHECKPOINT_SHA256 = "6020f8a6746639c0144eb979df0be0baa707af428d0e20a2db8991cf7452e5d6"
PINNED_REVISIONS = {"mmpose": "5408bc76f5b848cf925a0d1857899011d8c5b497",
                    "mmdetection": "fe3f809a0a514189baf889aa358c498d51ee36cd",
                    "mmdeploy": "bc75c9d6c8940aa03d0e1e5b5962bd930478ba77"}


def activate_pinned_vendor(root: Path) -> None:
    """Run against checked-out source, not an unrelated pip copy."""
    paths = []
    for name, revision in PINNED_REVISIONS.items():
        path = Path(root) / name
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if actual != revision:
            raise ValueError(f"{name} source revision mismatch: {actual} != {revision}")
        git_root = Path(subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "--show-toplevel"], text=True).strip())
        if git_root.resolve() != path.resolve():
            raise ValueError(f"{name} is not an independent pinned source checkout")
        # Include ignored generated files: an ignored Python module can still
        # shadow a pinned source import even when Git reports a clean HEAD.
        status = subprocess.check_output(
            ["git", "-C", str(path), "status", "--porcelain=v1", "-z",
             "--untracked-files=all", "--ignored=matching", "--ignore-submodules=none"])
        if status:
            raise ValueError(f"{name} pinned vendor working tree is dirty (tracked, untracked, or ignored files)")
        paths.append(str(path.resolve()))
    for path in reversed(paths):
        if path not in sys.path:
            sys.path.insert(0, path)
    # Importing pinned source must not dirty that checkout for a subsequent
    # exporter run in the same Python installation.
    sys.dont_write_bytecode = True

_INPUTS = {
    "detector": {"image_format": "rgba8-unorm", "color_order": "rgb",
                 "normalization": {"mean": [123.675, 116.28, 103.53],
                                   "norm": [1.0 / 58.395, 1.0 / 57.12, 1.0 / 57.375]},
                 "tensor_dtype": "fp16", "elempack": 1,
                 "width": 320, "height": 320, "crop": "letterbox", "pad_rgb": [114, 114, 114],
                 "resize_interpolation": "bilinear", "input_blob": "in0",
                 "raw_class_values": "logits", "raw_bbox_values": "stride_scaled_ltrb"},
    "body": {"image_format": "rgba8-unorm", "color_order": "rgb",
             "normalization": {"mean": [123.675, 116.28, 103.53],
                               "norm": [1.0 / 58.395, 1.0 / 57.12, 1.0 / 57.375]},
             "tensor_dtype": "fp16", "elempack": 4,
             "width": 192, "height": 256, "crop": "bbox_affine", "input_blob": "in0",
             "bbox_padding_factor": 1.25, "affine_interpolation": "bilinear", "use_udp": False,
             "simcc_split_ratio": 2.0, "keypoints": 26},
}
_OUTPUTS = {"detector": ("cls", "bbox"), "body": ("simcc_x", "simcc_y")}
MAX_OUTPUT_BYTES = 16 * 1024 * 1024  # Safety bound, not an asserted model shape.
DETECTOR_PROFILE_ID = "android-ncnn-vulkan"
DETECTOR_PROFILE_PATH = Path(__file__).resolve().parents[3] / "profiles" / f"{DETECTOR_PROFILE_ID}.json"
# The current TopDown runtime calls Detect(..., .35F, ...) in simcc_pipeline.cpp.
PINNED_PERSON_SCORE_THRESHOLD = 0.35


def load_detector_profile() -> tuple[Path, float]:
    """Use the checked-in production profile, never caller-supplied golden metadata."""
    path = DETECTOR_PROFILE_PATH
    profile = json.loads(path.read_text(encoding="utf-8"))
    if profile.get("schema_version") != 1 or profile.get("profile") != DETECTOR_PROFILE_ID:
        raise ValueError("detector profile identity or schema mismatch")
    threshold = profile.get("detector", {}).get("person_score_threshold")
    if type(threshold) not in (float, int) or threshold != PINNED_PERSON_SCORE_THRESHOLD:
        raise ValueError("detector profile person-score threshold differs from current runtime 0.35")
    return path, float(threshold)


def pinned_checkpoint_sha256(role: str) -> str:
    if role == "detector":
        return DETECTOR_CHECKPOINT_SHA256
    if role == "body":
        return POSE_CHECKPOINT_SHA256
    raise ValueError(f"Unknown model role: {role}")


def pinned_source_url(role: str) -> str:
    if role == "detector":
        return DETECTOR_CHECKPOINT_URL
    if role == "body":
        return POSE_CHECKPOINT_URL
    raise ValueError(f"Unknown model role: {role}")


def validate_output_contract(role: str, outputs: Mapping) -> None:
    if not isinstance(outputs, Mapping) or set(outputs) != set(output_blobs(role)):
        raise ValueError("output contract names must match pinned model blobs")
    total_bytes = 0
    for name in output_blobs(role):
        item = outputs[name]
        if not isinstance(item, Mapping) or set(item) != {"shape", "download_dtype", "max_bytes"}:
            raise ValueError(f"output contract fields missing for {name}")
        shape = item["shape"]
        if (not isinstance(shape, list) or not shape or
                any(type(n) is not int or n <= 0 for n in shape)):
            raise ValueError(f"output contract shape must be static positive for {name}")
        if item["download_dtype"] != "fp32":
            raise ValueError(f"output contract download dtype must be fp32 for {name}")
        elements = 1
        for dimension in shape:
            elements *= dimension
            if elements * 4 > MAX_OUTPUT_BYTES:
                raise ValueError(f"output contract exceeds safety byte bound for {name}")
        required_bytes = elements * 4
        if type(item["max_bytes"]) is not int or item["max_bytes"] != required_bytes:
            raise ValueError(f"output contract max_bytes must equal FP32 pack1 size for {name}")
        total_bytes += required_bytes
    if total_bytes > 2 * MAX_OUTPUT_BYTES:
        raise ValueError("Total output contract exceeds safety byte bound")
    if role == "detector":
        cls, bbox = outputs["cls"]["shape"], outputs["bbox"]["shape"]
        # The person-only classifier and LTRB regression must describe the
        # same dense candidate grid. Accept either common ONNX axis order.
        def axes(shape: list[int], width: int) -> int:
            if len(shape) != 3 or shape[0] != 1:
                raise ValueError("detector output requires batch-1 rank-3 tensors")
            if shape[2] == width and shape[1] > 1:
                return shape[1]
            if shape[1] == width and shape[2] > 1:
                return shape[2]
            raise ValueError("detector output has invalid candidate or coordinate axis")
        if axes(cls, 1) != axes(bbox, 4):
            raise ValueError("detector cls/bbox candidate axes differ")
    elif role == "body":
        ratio = model_input_contract(role)["simcc_split_ratio"]
        width = model_input_contract(role)["width"]
        height = model_input_contract(role)["height"]
        if outputs["simcc_x"]["shape"] != [1, 26, int(width * ratio)] or \
                outputs["simcc_y"]["shape"] != [1, 26, int(height * ratio)]:
            raise ValueError("Body26 SimCC output joint axis or split-ratio lengths mismatch")


def _require_metadata(manifest: Mapping, role: str, conversion: bool) -> None:
    fields = ("source_url", "source_revisions", "license", "opset", "export_command",
              "input_contract", "output_blobs", "output_contract")
    if conversion:
        fields += ("tool_version", "conversion_command", "onnx2ncnn_sha256", "ncnnoptimize_sha256")
    else:
        fields += ("export_tool",)
    for field in fields:
        if field not in manifest or manifest[field] in (None, "", [], {}):
            raise ValueError(f"{field} is required")
    if manifest["source_url"] != pinned_source_url(role):
        raise ValueError("source_url does not match pinned checkpoint")
    if manifest["source_revisions"] != PINNED_REVISIONS:
        raise ValueError("source_revisions do not match pinned sources")
    if type(manifest["opset"]) is not int or manifest["opset"] < 1:
        raise ValueError("opset must be a positive integer")
    if manifest["input_contract"] != model_input_contract(role):
        raise ValueError("input contract differs from pinned RGB FP16 contract for this model")
    if tuple(manifest["output_blobs"]) != output_blobs(role):
        raise ValueError("output_blobs differ from pinned names")
    validate_output_contract(role, manifest["output_contract"])
    if conversion:
        for name in ("onnx2ncnn_sha256", "ncnnoptimize_sha256"):
            value = manifest[name]
            if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise ValueError(f"{name} is not a canonical SHA-256")


def validate_export_manifest(manifest: Mapping, role: str, checkpoint: Path, onnx: Path) -> dict:
    """Fail before conversion tools can run, including on self-consistent forgery."""
    if manifest.get("role") != role:
        raise ValueError("Export role mismatch")
    _require_metadata(manifest, role, conversion=False)
    if manifest.get("checkpoint_sha256") != pinned_checkpoint_sha256(role):
        raise ValueError("checkpoint SHA-256 differs from pinned official source")
    require_artifact_hashes(checkpoint, onnx, manifest)
    from tools.models.ncnn.audit_ncnn_graph import audit_onnx
    input_contract = model_input_contract(role)
    graph = audit_onnx(onnx, output_blobs(role),
                       (1, 3, input_contract["height"], input_contract["width"]),
                       expected_output_contract=manifest["output_contract"])
    if graph["opset"].get("ai.onnx") != manifest["opset"]:
        raise ValueError("Export ONNX opset mismatch")
    return graph


def model_input_contract(role: str) -> dict:
    if role not in _INPUTS:
        raise ValueError(f"Unknown model role: {role}")
    return json.loads(json.dumps(_INPUTS[role]))


def output_blobs(role: str) -> tuple[str, ...]:
    if role not in _OUTPUTS:
        raise ValueError(f"Unknown model role: {role}")
    return _OUTPUTS[role]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_hash(path: Path, expected: str, label: str) -> str:
    if not isinstance(expected, str) or len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected):
        raise ValueError(f"{label} SHA-256 is missing or malformed")
    actual = sha256_file(Path(path))
    if actual != expected:
        raise ValueError(f"{label} SHA-256 mismatch: expected {expected}, actual {actual}")
    return actual


def require_artifact_hashes(checkpoint: Path, onnx: Path, manifest: Mapping) -> None:
    if "checkpoint_sha256" not in manifest:
        raise ValueError("checkpoint_sha256 is required")
    if "onnx_sha256" not in manifest:
        raise ValueError("onnx_sha256 is required")
    require_hash(checkpoint, manifest["checkpoint_sha256"], "checkpoint")
    require_hash(onnx, manifest["onnx_sha256"], "ONNX")


def canonical_json(value: Mapping) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n"


def build_provenance(metadata: Mapping, paths: Mapping[str, Path]) -> dict:
    role = str(metadata.get("role"))
    supplied = dict(metadata)
    supplied.setdefault("input_contract", model_input_contract(role))
    supplied.setdefault("output_blobs", list(output_blobs(role)))
    _require_metadata(supplied, role, conversion=True)
    required_paths = ("checkpoint", "onnx", "param", "bin", "fixture")
    for name in required_paths:
        if name not in paths:
            raise ValueError(f"{name} path is required")
    from tools.models.ncnn.audit_ncnn_graph import audit_onnx
    input_contract = model_input_contract(role)
    observed = audit_onnx(paths["onnx"], output_blobs(role),
                          (1, 3, input_contract["height"], input_contract["width"]),
                          expected_output_contract=supplied["output_contract"])
    if observed["opset"].get("ai.onnx") != supplied["opset"]:
        raise ValueError("ONNX opset differs from conversion provenance")
    return {"schema_version": 1, "role": role, "source_url": metadata["source_url"],
            "source_revisions": metadata["source_revisions"], "license": metadata["license"],
            "tool_version": metadata["tool_version"], "opset": metadata["opset"],
            "export_command": metadata["export_command"],
            "conversion_command": metadata["conversion_command"],
            "onnx2ncnn_sha256": metadata["onnx2ncnn_sha256"],
            "ncnnoptimize_sha256": metadata["ncnnoptimize_sha256"],
            "input_contract": model_input_contract(role),
            "output_blobs": list(output_blobs(role)),
            "output_contract": metadata["output_contract"],
            "artifacts": {f"{name}_sha256": sha256_file(paths[name]) for name in required_paths}}


def verify_provenance(manifest: Mapping, paths: Mapping[str, Path]) -> None:
    if manifest.get("schema_version") != 1:
        raise ValueError("Unsupported conversion provenance schema")
    role = manifest.get("role")
    _require_metadata(manifest, role, conversion=True)
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ValueError("Missing artifact hashes")
    if artifacts.get("checkpoint_sha256") != pinned_checkpoint_sha256(role):
        raise ValueError("checkpoint SHA-256 differs from pinned official source")
    for name in ("checkpoint", "onnx", "param", "bin", "fixture"):
        if name not in paths:
            raise ValueError(f"{name} path is required")
        require_hash(paths[name], artifacts.get(f"{name}_sha256"), name)
    for name in ("onnx2ncnn", "ncnnoptimize"):
        if name in paths:
            require_hash(paths[name], manifest[f"{name}_sha256"], name)


def write_manifest(path: Path, metadata: Mapping, paths: Mapping[str, Path]) -> None:
    Path(path).write_text(canonical_json(build_provenance(metadata, paths)), encoding="utf-8", newline="\n")


def record_golden(conversion: Mapping, paths: Mapping[str, Path]) -> dict:
    """Bind actual reference and two inference runs to accepted model bytes."""
    verify_provenance(conversion, paths)
    result = dict(conversion)
    result["artifacts"] = dict(conversion["artifacts"])
    for name in ("reference", "candidate", "repeat"):
        result["artifacts"][f"{name}_sha256"] = sha256_file(paths[name])
    if conversion["role"] == "detector":
        profile_path, _ = load_detector_profile()
        result["profile_id"] = DETECTOR_PROFILE_ID
        result["profile_sha256"] = sha256_file(profile_path)
    return result


def verify_golden_inputs(manifest: Mapping, paths: Mapping[str, Path]) -> None:
    verify_provenance(manifest, paths)
    if manifest["role"] == "detector":
        profile_path, _ = load_detector_profile()
        if manifest.get("profile_id") != DETECTOR_PROFILE_ID:
            raise ValueError("detector golden profile ID mismatch")
        require_hash(profile_path, manifest.get("profile_sha256"), "profile")
    for name in ("reference", "candidate", "repeat"):
        if name not in paths:
            raise ValueError(f"{name} path is required")
        require_hash(paths[name], manifest["artifacts"].get(f"{name}_sha256"), name)


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate export or record model-bound golden outputs")
    parser.add_argument("action", choices=("validate-export", "record-golden"))
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--role", choices=("detector", "body"))
    parser.add_argument("--output", type=Path)
    for name in ("checkpoint", "onnx", "param", "bin", "fixture", "reference", "candidate", "repeat"):
        parser.add_argument(f"--{name}", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if args.action == "validate-export":
        if not args.role or not args.checkpoint or not args.onnx:
            parser.error("validate-export requires --role, --checkpoint and --onnx")
        print(canonical_json(validate_export_manifest(manifest, args.role, args.checkpoint, args.onnx)))
        return
    names = ("checkpoint", "onnx", "param", "bin", "fixture", "reference", "candidate", "repeat")
    if not args.output or any(getattr(args, name) is None for name in names):
        parser.error("record-golden requires --output and all eight artifact paths")
    paths = {name: getattr(args, name) for name in names}
    args.output.write_text(canonical_json(record_golden(manifest, paths)), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
