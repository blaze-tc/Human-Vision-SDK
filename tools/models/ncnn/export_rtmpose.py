"""Static RTMPose-t Body26 SimCC export from a hash-pinned local checkpoint."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.models.ncnn.model_contract import (PINNED_REVISIONS, POSE_CHECKPOINT_URL,
                                              POSE_CHECKPOINT_SHA256, canonical_json,
                                              activate_pinned_vendor,
                                              model_input_contract, output_blobs,
                                              require_hash, sha256_file, validate_output_contract)


def export(checkpoint: Path, vendor_root: Path, reference_image: Path,
           output: Path, expected_checkpoint_sha256: str,
           expected_image_sha256: str, expected_onnx_sha256: str | None = None) -> dict:
    require_hash(checkpoint, expected_checkpoint_sha256, "checkpoint")
    if expected_checkpoint_sha256 != POSE_CHECKPOINT_SHA256:
        raise ValueError("Pose checkpoint is not the pinned official RTMPose-t Body26 checkpoint")
    require_hash(reference_image, expected_image_sha256, "reference image")
    activate_pinned_vendor(vendor_root)
    from mmdeploy.apis import torch2onnx
    from mmengine import Config

    deploy = vendor_root / "mmdeploy/configs/mmpose/pose-detection_simcc_ncnn-fp16_static-256x192.py"
    config = vendor_root / "mmpose/projects/rtmpose/rtmpose/body_2d_keypoint/rtmpose-t_8xb1024-700e_body8-halpe26-256x192.py"
    if not deploy.is_file() or not config.is_file():
        raise FileNotFoundError("Pinned RTMPose-t config or static ncnn deploy config missing")
    output.parent.mkdir(parents=True, exist_ok=True)
    static_deploy = Config.fromfile(str(deploy))
    static_deploy.onnx_config.input_names = ["in0"]
    deploy_copy = output.with_suffix(".deploy.py")
    static_deploy.dump(str(deploy_copy))
    torch2onnx(str(reference_image), str(output.parent), output.name, str(deploy_copy),
               str(config), str(checkpoint), "cpu")
    from tools.models.ncnn.audit_ncnn_graph import audit_onnx
    graph = audit_onnx(output, output_blobs("body"), (1, 3, 256, 192))
    validate_output_contract("body", graph["output_contract"])
    actual = sha256_file(output)
    if expected_onnx_sha256 and actual != expected_onnx_sha256:
        output.unlink()
        raise ValueError(f"ONNX SHA-256 mismatch: {actual} != {expected_onnx_sha256}")
    manifest = {"role": "body", "source_revisions": PINNED_REVISIONS,
                "source_url": POSE_CHECKPOINT_URL,
                "license": "OpenMMLab source Apache-2.0; verify model and dataset terms",
                "checkpoint_sha256": expected_checkpoint_sha256,
                "reference_image_sha256": expected_image_sha256, "onnx_sha256": actual,
                "opset": graph["opset"]["ai.onnx"], "input_contract": model_input_contract("body"),
                "output_blobs": list(output_blobs("body")),
                "output_contract": graph["output_contract"],
                "export_command": "python -m tools.models.ncnn.export_rtmpose --checkpoint <pinned> --vendor-root <pinned> --reference-image <pinned> --output <onnx>",
                "export_tool": "MMDeploy 1.3.1"}
    output.with_suffix(".export.json").write_text(canonical_json(manifest), encoding="utf-8", newline="\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--vendor-root", type=Path, required=True)
    parser.add_argument("--reference-image", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-checkpoint-sha256", required=True)
    parser.add_argument("--expected-image-sha256", required=True)
    parser.add_argument("--expected-onnx-sha256")
    args = parser.parse_args()
    print(canonical_json(export(args.checkpoint, args.vendor_root, args.reference_image,
                                args.output, args.expected_checkpoint_sha256,
                                args.expected_image_sha256, args.expected_onnx_sha256)))


if __name__ == "__main__":
    main()
