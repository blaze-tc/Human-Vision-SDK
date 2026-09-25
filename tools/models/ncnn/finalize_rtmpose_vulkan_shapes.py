"""Hash-pinned shape-only rewrite for the official RTMPose Body26 ncnn graph.

Each tuple is (original layer type, ONNX/ncnn axis, output ncnn shape).
Shapes use ncnn's (w, h, [c]) parameter order. All affected 2D tensors
have h=26; all affected 3D tensors have c=1 or c=26, so Vulkan Reshape
selects pack1. Both original CPU layers and Reshape preserve flat order.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

SOURCE_SHA256 = "170dc71af088157b013338a3a437039882bf431ad0e9d00baea1763130ff2c5e"
LAYERS = {
    "/mlp/mlp.0/Unsqueeze": ("ExpandDims", 1, (48, 1, 26)),
    "/mlp/mlp.0/Unsqueeze_1": ("ExpandDims", 1, (1, 1, 26)),
    "/mlp/mlp.0/Squeeze": ("Squeeze", 1, (48, 26)),
    "/gau/ln/Unsqueeze": ("ExpandDims", 1, (256, 1, 26)),
    "/gau/ln/Unsqueeze_1": ("ExpandDims", 1, (1, 1, 26)),
    "/gau/ln/Squeeze": ("Squeeze", 1, (256, 26)),
    "/gau/Unsqueeze": ("ExpandDims", 0, (128, 26, 1)),
    "/gau/Squeeze": ("Squeeze", 0, (128, 26)),
    "/gau/Unsqueeze_1": ("ExpandDims", 0, (128, 26, 1)),
    "/gau/Squeeze_1": ("Squeeze", 0, (128, 26)),
    "/gau/res_scale/Unsqueeze": ("ExpandDims", 0, (256, 26, 1)),
    "/gau/res_scale/Squeeze": ("Squeeze", 0, (256, 26)),
}


def finalize(source: Path, target: Path) -> dict:
    raw = source.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != SOURCE_SHA256:
        raise ValueError(f"Source graph SHA-256 mismatch: {actual}")
    lines = raw.decode("utf-8").splitlines()
    if lines[:2] != ["7767517", "166 188"]:
        raise ValueError("Unexpected ncnn graph header")
    first_conv = lines[8].split()
    if (first_conv[:6] != ["Convolution", "/backbone/stem/stem.0/conv/Conv", "1", "1", "in0", "/backbone/stem/stem.0/conv/Conv_output_0"]
            or "6=432" not in first_conv):
        raise ValueError("Unexpected padded first Conv")
    seen = set()
    for index in range(2, len(lines)):
        parts = lines[index].split()
        if not parts or parts[0] not in ("ExpandDims", "Squeeze"):
            continue
        if len(parts) != 7 or parts[1] not in LAYERS or parts[1] in seen:
            raise ValueError("Unexpected shape layer layout")
        expected_type, axis, shape = LAYERS[parts[1]]
        if parts[0] != expected_type or parts[-1] != f"-23303=1,{axis}" or parts[2:4] != ["1", "1"]:
            raise ValueError(f"Unexpected shape layer {parts[1]}")
        seen.add(parts[1])
        parts[0] = "Reshape"
        parts[-1] = " ".join(f"{key}={value}" for key, value in zip((0, 1, 2), shape))
        lines[index] = " ".join(parts)
    if seen != LAYERS.keys():
        raise ValueError(f"Shape layer set mismatch: {sorted(LAYERS.keys() - seen)}")
    target.parent.mkdir(parents=True, exist_ok=True)
    output = ("\n".join(lines) + "\n").encode("utf-8")
    target.write_bytes(output)
    return {"rewritten_layers": len(seen), "source_sha256": actual,
            "param_sha256": hashlib.sha256(output).hexdigest()}
