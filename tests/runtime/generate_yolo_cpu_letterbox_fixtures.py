"""Generate ignored CPU-only OpenCV/ORT letterbox acceptance oracles.

Uses the pinned local ONNX and original saved RGB controls; never downloads or
redistributes weights. Geometry/decoding are independent of the C++ pipeline.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

import cv2
import numpy as np
import onnxruntime as ort

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools/models/ncnn"))
from yolo_pose_gate import decode

ONNX_SHA = "6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3"
SOURCE_SHA = {
    "seven": "814e663406f6756414768311ea62dfb364cd2bc9e0e6cf72bb1b4422ef8bd3c6",
    "one": "a8f2701bf39d8e91f731f58de71e68f24ff4285ce36c9379d292f69cec344f6e",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(root, output):
    model = root / "recovered-low.onnx"
    if sha(model) != ONNX_SHA:
        raise ValueError("Canonical ONNX SHA mismatch")
    controls = {}
    for name, expected in SOURCE_SHA.items():
        path = root / name / "source.rgb"
        if sha(path) != expected:
            raise ValueError(f"Pinned {name} RGB source SHA mismatch")
        controls[name] = np.fromfile(path, np.uint8).reshape(576, 1024, 3)
    landscape = np.full((480, 640, 3), 114, np.uint8)
    landscape[60:420] = cv2.resize(controls["seven"], (640, 360), interpolation=cv2.INTER_LINEAR)
    portrait = controls["one"][115:461, 403:621].copy()
    odd = np.full((347, 219, 3), 114, np.uint8)
    odd[:346, :218] = portrait
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    session = ort.InferenceSession(str(model), sess_options=options, providers=["CPUExecutionProvider"])
    output.mkdir(parents=True, exist_ok=True)
    receipts = []
    for name, source in (("landscape4x3", landscape), ("portrait", portrait), ("portrait-odd", odd)):
        sh, sw = source.shape[:2]
        scale = min(512 / sw, 288 / sh)
        rw, rh = (512, int(sh * scale)) if sw * 288 >= sh * 512 else (int(sw * scale), 288)
        left, top = (512 - rw) // 2, (288 - rh) // 2
        canvas = np.full((288, 512, 3), 114, np.uint8)
        canvas[top:top + rh, left:left + rw] = cv2.resize(source, (rw, rh), interpolation=cv2.INTER_LINEAR)
        tensor = np.ascontiguousarray(canvas.transpose(2, 0, 1), dtype=np.float32) / np.float32(255)
        values = session.run(["out0", "out1"], {"in0": tensor[None]})
        geometry = dict(width=512, height=288, resized_width=rw, resized_height=rh,
                        source_width=sw, source_height=sh, left=left, top=top, scale=scale)
        bodies = decode(values[0].reshape(3024, 65), values[1].reshape(3024, 51), geometry)
        files = {"source.rgb": source, "input.fp32": tensor, "out0.fp32": values[0], "out1.fp32": values[1]}
        directory = output / name
        directory.mkdir(exist_ok=True)
        for filename, value in files.items():
            value.tofile(directory / filename)
        receipt = dict(name=name, onnx_sha256=ONNX_SHA, source_control_sha256=SOURCE_SHA,
                       opencv_version=cv2.__version__, onnxruntime_version=ort.__version__,
                       provider=session.get_providers(), geometry=geometry,
                       expected_people=len(bodies), bodies=bodies,
                       sha256={filename: sha(directory / filename) for filename in files})
        (directory / "receipt.json").write_text(json.dumps(receipt, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        receipts.append(dict(name=name, expected_people=len(bodies), geometry=geometry,
                             receipt_sha256=sha(directory / "receipt.json"), sha256=receipt["sha256"]))
    report = dict(onnx_sha256=ONNX_SHA, fixtures=receipts)
    (output / "index.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=PROJECT / "out/rknn-validation-20261009")
    parser.add_argument("--output", type=Path, default=PROJECT / "out/yolo-cpu-letterbox-fixtures")
    args = parser.parse_args()
    generate(args.root, args.output)
