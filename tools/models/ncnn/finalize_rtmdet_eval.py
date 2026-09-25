"""Reproduce the pinned RTMDet Nano local-only ncnn detector from C1 assets.

The converter omits seven static shape fields. This script permits precisely
those audited edits and rejects every other source or resulting graph byte.
"""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from tools.models.ncnn.audit_ncnn_graph import audit_param
from tools.models.ncnn.model_contract import (
    DETECTOR_CHECKPOINT_SHA256, build_provenance, canonical_json,
    model_input_contract, output_blobs, require_hash,
)

ROOT = Path(__file__).resolve().parents[3]
RAW_PARAM_SHA256 = "8fd7ccb55c9e28161686e748fb0dd6bd042716d45f614f837586c9a126e35346"
FINAL_PARAM_SHA256 = "9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3"
BIN_SHA256 = "4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da"
ONNX_SHA256 = "3c4a7a1a2fb500678d7b01ebef87c119ce85626667aaf562109d3622b8abeed5"
CONVERTERS = {
    "onnx2ncnn": (ROOT / "out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe",
                  "b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb"),
    "ncnnoptimize": (ROOT / "out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe",
                     "40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7"),
}
DIMENSIONS = {"/Reshape": 1600, "/Reshape_1": 400, "/Reshape_2": 100,
              "/Reshape_3": 1600, "/Reshape_4": 400, "/Reshape_5": 100}


def corrected_rtmdet_param(raw_text: str) -> str:
    lines = raw_text.splitlines()
    if lines[:2] != ["7767517", "316 345"] or len(lines) != 318:
        raise ValueError("RTMDet raw graph must contain exactly 316 layers and 345 blobs")
    seen = set()
    for index, line in enumerate(lines[2:], 2):
        fields = line.split()
        if fields[:2] == ["Input", "in0"]:
            if fields != ["Input", "in0", "0", "1", "in0"] or "in0" in seen:
                raise ValueError("Unexpected raw Input dimensions or duplicate Input")
            lines[index] = line + " 0=320 1=320 2=3"
            seen.add("in0")
        elif fields and fields[0] == "Reshape":
            name = fields[1]
            if (name not in DIMENSIONS or name in seen or
                    fields[-1] != "1=-1" or
                    fields[-2] != ("0=1" if name in tuple(DIMENSIONS)[:3] else "0=4")):
                raise ValueError(f"Unexpected raw Reshape dimensions: {name}")
            lines[index] = line[:-4] + f"1={DIMENSIONS[name]}"
            seen.add(name)
        elif fields and fields[0] == "Input":
            raise ValueError("Unexpected raw Input blob")
    if seen != set(DIMENSIONS) | {"in0"}:
        raise ValueError("Missing audited Input or Reshape field")
    return "\n".join(lines) + "\n"


def finalize(checkpoint: Path, onnx: Path, raw_param: Path, weights: Path,
             output: Path, *, fixture: Path | None = None,
             export_manifest: Path | None = None,
             input_override: dict | None = None) -> dict:
    """Validate pinned inputs, write ignored local assets, and return their manifest."""
    # Check the official source before touching the output directory.
    require_hash(checkpoint, DETECTOR_CHECKPOINT_SHA256, "checkpoint")
    require_hash(onnx, ONNX_SHA256, "ONNX")
    require_hash(raw_param, RAW_PARAM_SHA256, "optimized raw param")
    require_hash(weights, BIN_SHA256, "ncnn weights")
    for name, (path, digest) in CONVERTERS.items():
        require_hash(path, digest, name)
    contract = model_input_contract("detector")
    if input_override:
        contract.update(input_override)
    if contract != model_input_contract("detector"):
        raise ValueError("Detector requires RGB 320x320 FP16 pack1 at in0")
    corrected = corrected_rtmdet_param(raw_param.read_text(encoding="utf-8"))
    import hashlib
    # ncnnoptimize emits CRLF on this pinned Windows toolchain; retain its
    # line-ending convention so the corrected graph matches the C2 golden hash.
    corrected_bytes = corrected.replace("\n", "\r\n").encode("utf-8")
    if hashlib.sha256(corrected_bytes).hexdigest() != FINAL_PARAM_SHA256:
        raise ValueError("Corrected RTMDet param SHA-256 mismatch")
    audit_param(corrected, output_blobs("detector"), (320, 320, 3))
    fixture = fixture or ROOT / "out/c2-detector/golden/official/image.png"
    export_manifest = export_manifest or ROOT / "out/c2-detector/rtmdet-nano.export.json"
    metadata = json.loads(export_manifest.read_text(encoding="utf-8"))
    metadata.update(tool_version="ncnn 20260526",
                    onnx2ncnn_sha256=CONVERTERS["onnx2ncnn"][1],
                    ncnnoptimize_sha256=CONVERTERS["ncnnoptimize"][1],
                    conversion_command="onnx2ncnn pinned ONNX; ncnnoptimize 65536; audited seven-field static correction")
    output.mkdir(parents=True, exist_ok=True)
    (output / "model.param").write_bytes(corrected_bytes)
    shutil.copyfile(weights, output / "model.bin")
    paths = {"checkpoint": checkpoint, "onnx": onnx, "param": output / "model.param",
             "bin": output / "model.bin", "fixture": fixture}
    manifest = build_provenance(metadata, paths)
    manifest["local_evaluation_only"] = True
    (output / "model.json").write_text(canonical_json(manifest), encoding="utf-8", newline="\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, default=ROOT / "out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth")
    parser.add_argument("--onnx", type=Path, default=ROOT / "out/c2-detector/rtmdet-nano.onnx")
    parser.add_argument("--raw-param", type=Path, default=ROOT / "out/c2-local-detector/rebuild/optimized.param")
    parser.add_argument("--bin", type=Path, default=ROOT / "out/c2-local-detector/rebuild/optimized.bin")
    parser.add_argument("--output", type=Path, default=ROOT / "out/c2-local-detector")
    args = parser.parse_args()
    manifest = finalize(args.checkpoint, args.onnx, args.raw_param, args.bin, args.output)
    print(canonical_json({"output": str(args.output), "artifacts": manifest["artifacts"],
                          "local_evaluation_only": manifest["local_evaluation_only"]}), end="")


if __name__ == "__main__":
    main()
