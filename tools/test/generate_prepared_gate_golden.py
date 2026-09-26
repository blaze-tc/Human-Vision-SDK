"""Reproduce the Task 4 four-quadrant source and channel-major FP16 tensor.

This CPU computation creates only an offline reference. The Android runtime
must independently produce the tensor via Unity Vulkan -> AHB -> ncnn Vulkan.
"""

import argparse
import hashlib
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "tests/fixtures/ncnn_prepared_detector_contract.json"
EVIDENCE = ROOT / "docs/validation/PREPARED_DETECTOR_INPUT_GOLDEN.json"
LOCAL_PACK = ROOT / "out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16"
COLORS = ((128, 64, 32, 255), (48, 160, 210, 255),
          (220, 118, 44, 255), (22, 35, 235, 255))


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def f32_to_fp16_rtz_bits(value):
    """Convert a finite float32 to FP16 with round-toward-zero semantics.

    ncnn's Vulkan packing shader truncates the normalized FP32 value on the
    observed Snapdragon 888 path. Python's '<e' pack rounds to nearest-even.
    """
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    sign = (bits >> 16) & 0x8000
    exponent = (bits >> 23) & 0xff
    mantissa = bits & 0x7fffff
    if exponent == 0xff:
        raise ValueError("Prepared detector FP16 reference requires finite inputs")
    half_exponent = exponent - 127 + 15
    if half_exponent >= 31:
        return sign | 0x7bff  # finite overflow under round-toward-zero
    if half_exponent <= 0:
        if half_exponent < -10:
            return sign
        return sign | ((mantissa | 0x800000) >> (14 - half_exponent))
    return sign | (half_exponent << 10) | (mantissa >> 13)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def generate():
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    if (contract["width"], contract["height"], contract["image_format"],
            contract["color_order"], contract["tensor_dtype"],
            contract["elempack"]) != (320, 320, "rgba8-unorm", "rgb", "fp16", 1):
        raise ValueError("Prepared detector reference contract changed")
    mean = [f32(x) for x in contract["mean"]]
    norm = [f32(x) for x in contract["norm"]]
    pixels = bytes(channel for y in range(320) for x in range(320)
                   for channel in COLORS[(2 if y >= 160 else 0) + (1 if x >= 160 else 0)])
    channel_planes = []
    samples = {}
    for channel in range(3):
        plane = bytearray()
        for y in range(320):
            for x in range(320):
                source = COLORS[(2 if y >= 160 else 0) + (1 if x >= 160 else 0)][channel]
                value = f32(f32(float(source) - mean[channel]) * norm[channel])
                plane.extend(struct.pack("<H", f32_to_fp16_rtz_bits(value)))
                if (x, y) in ((80, 80), (240, 80), (80, 240), (240, 240),
                              (0, 0), (319, 319)):
                    samples.setdefault(f"{x},{y}", [None, None, None])[channel] = \
                        struct.unpack("<e", plane[-2:])[0]
        channel_planes.append(plane)
    reference = {
        "fixture": "320x320 four-quadrant RGBA8, bottom-up source rows",
        "colors_rgba": [list(color) for color in COLORS],
        "fixture_rgba_sha256": sha256(pixels),
        "contract_sha256": sha256(json.dumps(contract, sort_keys=True,
                                             separators=(",", ":")).encode("utf-8")),
        "modelpack": contract["modelpack"],
        "model_param_sha256": contract["param_sha256"],
        "model_bin_sha256": contract["bin_sha256"],
        "output_max_logical_bytes": contract["output_max_logical_bytes"],
        "tensor_layout": "RGB channel-major, 320x320, FP16 little-endian pack1",
        "tensor_conversion": "FP32 to FP16 round toward zero (RTZ)",
        "conversion_scope": "Observed Snapdragon 888 ncnn Vulkan packing semantics; other devices remain device-gated",
        "tensor_fp16_sha256": sha256(b"".join(channel_planes)),
        "sample_rgb_fp16": samples,
    }
    if LOCAL_PACK.is_dir():
        pack = json.loads((LOCAL_PACK / "modelpack.json").read_text(encoding="utf-8"))
        model = next(model for model in pack["models"] if model["role"] == "detector")
        source_contract = model["input_contract"]
        for field in ("width", "height", "image_format", "color_order", "tensor_dtype",
                      "elempack"):
            if source_contract[field] != contract[field]:
                raise ValueError(f"Local ModelPack {field} differs from pinned fixture")
        if source_contract["normalization"] != {"mean": contract["mean"],
                                                   "norm": contract["norm"]}:
            raise ValueError("Local ModelPack normalization differs from pinned fixture")
        if model["output_contract"]["max_output_bytes"] != contract["output_max_logical_bytes"]:
            raise ValueError("Local ModelPack output bounds differ from pinned fixture")
        for field, filename in (("param_sha256", "model.param"),
                                ("bin_sha256", "model.bin")):
            if model[field] != contract[field] or \
                    sha256((LOCAL_PACK / "detector" / filename).read_bytes()) != contract[field]:
                raise ValueError(f"Local ModelPack {filename} hash differs from pinned fixture")
    return reference


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true", help="write checked JSON evidence")
    parser.add_argument("--verify", action="store_true", help="compare checked JSON evidence")
    args = parser.parse_args()
    result = generate()
    if args.verify and json.loads(EVIDENCE.read_text(encoding="utf-8")) != result:
        raise SystemExit("Prepared detector golden evidence differs from pinned contract")
    if args.write:
        EVIDENCE.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
