"""Compare decoded Body26 source-space joints after affine reversal."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

import numpy as np

from tools.models.ncnn.model_contract import verify_golden_inputs


def fixture_image_size(path: Path) -> tuple[int, int]:
    """Read geometry from the same image whose SHA-256 golden manifest binds."""
    from PIL import Image

    with Image.open(path) as image:
        size = image.size
        image.verify()
    if len(size) != 2 or any(type(n) is not int or n <= 0 for n in size):
        raise ValueError("pose fixture image has invalid geometry")
    return size


def _arrays(result: dict) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    points = np.asarray(result["points"], dtype=np.float64)
    scores = np.asarray(result["scores"], dtype=np.float64)
    valid = np.asarray(result["valid"])
    if points.shape != (26, 2) or scores.shape != (26,) or valid.shape != (26,) or valid.dtype != np.bool_:
        raise ValueError("Expected Body26 points, confidence, valid mask")
    if not np.isfinite(points).all() or not np.isfinite(scores).all():
        raise ValueError("Nonfinite pose output")
    return points, scores, valid


def compare_pose(reference: dict, candidate: dict, bbox: list[float],
                 image_size: tuple[int, int], repeat: dict | None = None) -> dict:
    if len(bbox) != 4 or not all(math.isfinite(x) for x in bbox):
        raise ValueError("Invalid bbox")
    if (len(image_size) != 2 or any(type(n) is not int or n <= 0 for n in image_size) or
            bbox[0] < 0 or bbox[1] < 0 or bbox[2] > image_size[0] or bbox[3] > image_size[1] or
            bbox[2] <= bbox[0] or bbox[3] <= bbox[1]):
        raise ValueError("pose bbox must be positive, ordered, and inside hashed fixture image")
    diagonal = math.hypot(bbox[2] - bbox[0], bbox[3] - bbox[1])
    if diagonal <= 0:
        raise ValueError("Degenerate bbox")
    ref_points, ref_scores, ref_valid = _arrays(reference)
    got_points, got_scores, got_valid = _arrays(candidate)
    if not np.array_equal(ref_valid, got_valid):
        raise ValueError("valid-joint mask mismatch")
    if int(ref_valid.sum()) < 3:
        raise ValueError("Insufficient valid-joint coverage for Body26 golden comparison")
    distances = np.linalg.norm(ref_points[ref_valid] - got_points[ref_valid], axis=1) / diagonal
    score_error = np.abs(ref_scores[ref_valid] - got_scores[ref_valid])
    p95_distance = float(np.percentile(distances, 95)) if distances.size else 0.0
    max_distance = float(np.max(distances)) if distances.size else 0.0
    p95_confidence = float(np.percentile(score_error, 95)) if score_error.size else 0.0
    if p95_distance > 0.01 or max_distance > 0.03:
        raise ValueError(f"pose distance gate failed: p95={p95_distance}, max={max_distance}")
    if p95_confidence > 0.02:
        raise ValueError(f"pose confidence gate failed: p95={p95_confidence}")
    if repeat is not None:
        compare_pose(candidate, repeat, bbox, image_size)
        compare_pose(reference, repeat, bbox, image_size)
    return {"valid_count": int(ref_valid.sum()), "distance_p95": p95_distance,
            "distance_max": max_distance, "confidence_error_p95": p95_confidence}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--repeat", type=Path, required=True)
    for name in ("checkpoint", "onnx", "param", "bin"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    for name in ("onnx2ncnn", "ncnnoptimize"):
        parser.add_argument(f"--{name}", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("role") != "body":
        raise ValueError("Expected Body26 provenance manifest")
    verify_golden_inputs(manifest, {name: getattr(args, name) for name in
                                    ("checkpoint", "onnx", "param", "bin", "fixture",
                                     "reference", "candidate", "repeat", "onnx2ncnn", "ncnnoptimize")
                                    if getattr(args, name) is not None})
    read = lambda path: json.loads(path.read_text(encoding="utf-8"))
    reference, candidate, repeat = read(args.reference), read(args.candidate), read(args.repeat)
    bbox = reference["bbox"]
    for label, result in (("candidate", candidate), ("repeat", repeat)):
        if "bbox" in result and result["bbox"] != bbox:
            raise ValueError(f"{label} bbox differs from hashed reference crop metadata")
    print(json.dumps(compare_pose(reference, candidate, bbox, fixture_image_size(args.fixture), repeat),
                     sort_keys=True))


if __name__ == "__main__":
    main()
