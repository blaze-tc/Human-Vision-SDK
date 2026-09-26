"""Local-only detector diagnostic for user-supplied video frames.

This runs the pinned ONNX reference on exactly the C2 detector golden input
contract. It does not run in the SDK or replace the Android GPU gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

PINNED_ONNX_SHA256 = "3c4a7a1a2fb500678d7b01ebef87c119ce85626667aaf562109d3622b8abeed5"
MEAN = np.array([123.675, 116.28, 103.53], np.float32)
NORM = np.array([0.017124753831663668, 0.01750700280112045,
                 0.017429193899782137], np.float32)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def preprocess(image: np.ndarray) -> np.ndarray:
    height, width = image.shape[:2]
    scale = min(320 / width, 320 / height)
    scaled_width, scaled_height = round(width * scale), round(height * scale)
    left, top = (320 - scaled_width) // 2, (320 - scaled_height) // 2
    padded = np.full((320, 320, 3), 114, np.uint8)
    padded[top:top + scaled_height, left:left + scaled_width] = cv2.resize(
        image, (scaled_width, scaled_height), interpolation=cv2.INTER_LINEAR)
    rgb = padded[:, :, ::-1].astype(np.float32)
    return ((rgb - MEAN) * NORM).transpose(2, 0, 1)[None].copy()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--video", action="append", required=True, type=Path)
    parser.add_argument("--seconds", nargs="+", default=["5"], type=float)
    args = parser.parse_args()
    if sha256_file(args.model) != PINNED_ONNX_SHA256:
        raise ValueError("Pinned detector ONNX SHA-256 mismatch")
    session = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    results = []
    for video in args.video:
        video_hash = sha256_file(video)
        capture = cv2.VideoCapture(str(video))
        if not capture.isOpened():
            raise ValueError(f"Cannot open video: {video}")
        fps = capture.get(cv2.CAP_PROP_FPS)
        for seconds in args.seconds:
            index = round(seconds * fps)
            capture.set(cv2.CAP_PROP_POS_FRAMES, index)
            ok, image = capture.read()
            if not ok:
                raise ValueError(f"Cannot read {video} frame {index}")
            tensor = preprocess(image)
            cls, bbox = session.run(["cls", "bbox"], {"in0": tensor})
            logits = cls.reshape(-1)
            scores = 1 / (1 + np.exp(-logits))
            results.append({
                "video": str(video), "video_sha256": video_hash,
                "frame_index": index, "frame_bgr_sha256": sha256_bytes(image.tobytes()),
                "width": image.shape[1], "height": image.shape[0],
                "input_fp32_sha256": sha256_bytes(tensor.tobytes()),
                "onnx_sha256": PINNED_ONNX_SHA256,
                "max_raw_logit": float(logits.max()),
                "max_score": float(scores.max()),
                "candidates_at_0_35": int(np.count_nonzero(scores >= 0.35)),
                "top_index": int(logits.argmax()),
                "bbox_shape": list(bbox.shape),
            })
        capture.release()
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
