"""Export pinned RTMDet Nano head logits/distances, leaving decode/NMS to TopDown.

This is a conversion candidate, not an assertion of ncnn operator closure or parity.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.models.ncnn.model_contract import (DETECTOR_CHECKPOINT_SHA256,
                                              PINNED_REVISIONS, canonical_json,
                                              activate_pinned_vendor,
                                              model_input_contract, output_blobs,
                                              require_hash, sha256_file, validate_output_contract)


def export(checkpoint: Path, vendor_root: Path, output: Path, expected_checkpoint_sha256: str,
           expected_onnx_sha256: str | None = None) -> dict:
    require_hash(checkpoint, expected_checkpoint_sha256, "checkpoint")
    if expected_checkpoint_sha256 != DETECTOR_CHECKPOINT_SHA256:
        raise ValueError("Detector checkpoint is not the pinned official Nano checkpoint")
    activate_pinned_vendor(vendor_root)
    import torch
    from mmdet.apis import init_detector

    config = vendor_root / "mmpose/projects/rtmpose/rtmdet/person/rtmdet_nano_320-8xb32_coco-person.py"
    model = init_detector(str(config), str(checkpoint), device="cpu")
    model.eval()

    class RawHead(torch.nn.Module):
        def __init__(self, detector):
            super().__init__()
            self.detector = detector

        def forward(self, image):
            features = self.detector.extract_feat(image)
            cls_levels, bbox_levels = self.detector.bbox_head(features)
            # Keep raw per-anchor logits and distance predictions; the runtime
            # owns grid/stride decode, thresholding and NMS.
            cls = torch.cat([x.permute(0, 2, 3, 1).reshape(1, -1, x.shape[1])
                             for x in cls_levels], dim=1)
            bbox = torch.cat([x.permute(0, 2, 3, 1).reshape(1, -1, 4)
                              for x in bbox_levels], dim=1)
            return cls, bbox

    output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(RawHead(model).eval(), torch.zeros(1, 3, 320, 320), str(output),
                      input_names=["in0"], output_names=list(output_blobs("detector")),
                      opset_version=17, do_constant_folding=True,
                      dynamic_axes=None)
    from tools.models.ncnn.audit_ncnn_graph import audit_onnx
    graph = audit_onnx(output, output_blobs("detector"), (1, 3, 320, 320))
    validate_output_contract("detector", graph["output_contract"])
    actual = sha256_file(output)
    if expected_onnx_sha256 and actual != expected_onnx_sha256:
        output.unlink()
        raise ValueError(f"ONNX SHA-256 mismatch: {actual} != {expected_onnx_sha256}")
    manifest = {"role": "detector", "source_revisions": PINNED_REVISIONS,
                "source_url": "https://download.openmmlab.com/mmpose/v1/projects/rtmpose/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth",
                "license": "OpenMMLab source Apache-2.0; verify model and dataset terms",
                "checkpoint_sha256": expected_checkpoint_sha256, "onnx_sha256": actual,
                "opset": graph["opset"]["ai.onnx"], "input_contract": model_input_contract("detector"),
                "output_blobs": list(output_blobs("detector")),
                "output_contract": graph["output_contract"],
                "export_command": "python -m tools.models.ncnn.export_rtmdet_nano --checkpoint <pinned> --vendor-root <pinned> --output <onnx>",
                "export_tool": f"torch {torch.__version__}"}
    output.with_suffix(".export.json").write_text(canonical_json(manifest), encoding="utf-8", newline="\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--vendor-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-checkpoint-sha256", required=True)
    parser.add_argument("--expected-onnx-sha256")
    args = parser.parse_args()
    print(canonical_json(export(args.checkpoint, args.vendor_root, args.output,
                                args.expected_checkpoint_sha256, args.expected_onnx_sha256)))


if __name__ == "__main__":
    main()
