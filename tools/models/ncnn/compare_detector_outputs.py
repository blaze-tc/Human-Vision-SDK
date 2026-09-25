"""Compare decoded, person-only, post-NMS reference and ncnn detections."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.models.ncnn.model_contract import load_detector_profile, verify_golden_inputs


def _iou(a: list[float], b: list[float]) -> float:
    if len(a) != 4 or len(b) != 4 or not all(math.isfinite(x) for x in a + b):
        raise ValueError("Invalid bbox")
    if a[2] <= a[0] or a[3] <= a[1] or b[2] <= b[0] or b[3] <= b[1]:
        raise ValueError("Degenerate bbox")
    left, top = max(a[0], b[0]), max(a[1], b[1])
    right, bottom = min(a[2], b[2]), min(a[3], b[3])
    overlap = max(0.0, right - left) * max(0.0, bottom - top)
    area_a = (a[2] - a[0]) * (a[3] - a[1])
    area_b = (b[2] - b[0]) * (b[3] - b[1])
    return overlap / (area_a + area_b - overlap)


def compare_detector(reference: list[dict], candidate: list[dict], threshold: float,
                     repeat: list[dict] | None = None) -> dict:
    if not math.isfinite(threshold) or not 0 <= threshold <= 1:
        raise ValueError("Invalid person threshold")
    for detection in reference + candidate:
        if detection.get("person", True) is not True:
            raise ValueError("person-only detector golden output contains a nonperson entry")
        score = detection["score"]
        if not isinstance(score, (int, float)) or not math.isfinite(score) or not 0 <= score <= 1:
            raise ValueError("Invalid detector score")
        _iou(detection["bbox"], detection["bbox"])
    expected = [box for box in reference if box["score"] >= threshold]
    actual = [box for box in candidate if box["score"] >= threshold]
    if len(expected) != len(actual):
        raise ValueError(f"candidate count mismatch or missed reference person: {len(expected)} vs {len(actual)}")
    all_ious = [[_iou(ref["bbox"], got["bbox"]) for got in actual] for ref in expected]
    edges = []
    for i, ref in enumerate(expected):
        row = [j for j, got in enumerate(actual)
               if all_ious[i][j] >= 0.95 and abs(ref["score"] - got["score"]) <= 0.01]
        row.sort(key=lambda j: (-all_ious[i][j], j))
        if not row:
            if any(value >= 0.95 for value in all_ious[i]):
                raise ValueError(f"detector score mismatch for reference {i}")
            raise ValueError(f"bbox IoU below 0.95 for reference {i}")
        edges.append(row)

    # Augmenting paths find a complete feasible one-to-one match even when the
    # highest-IoU first choice would steal another reference's only match.
    assigned: dict[int, int] = {}

    def augment(reference_index: int, visited: set[int]) -> bool:
        for candidate_index in edges[reference_index]:
            if candidate_index in visited:
                continue
            visited.add(candidate_index)
            if candidate_index not in assigned or augment(assigned[candidate_index], visited):
                assigned[candidate_index] = reference_index
                return True
        return False

    if not all(augment(i, set()) for i in range(len(expected))):
        raise ValueError("No complete one-to-one detector matching satisfies IoU and score gates")
    matches = [{"iou": all_ious[i][j],
                "score_error": abs(expected[i]["score"] - actual[j]["score"])}
               for j, i in sorted(assigned.items(), key=lambda item: item[1])]
    if repeat is not None:
        compare_detector(candidate, repeat, threshold)
        compare_detector(reference, repeat, threshold)
    return {"candidate_count": len(expected), "matches": matches}


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
    if manifest.get("role") != "detector":
        raise ValueError("Expected detector provenance manifest")
    verify_golden_inputs(manifest, {name: getattr(args, name) for name in
                                    ("checkpoint", "onnx", "param", "bin", "fixture",
                                     "reference", "candidate", "repeat", "onnx2ncnn", "ncnnoptimize")
                                    if getattr(args, name) is not None})
    read = lambda path: json.loads(path.read_text(encoding="utf-8"))["detections"]
    _, threshold = load_detector_profile()
    print(json.dumps(compare_detector(read(args.reference), read(args.candidate), threshold,
                                      repeat=read(args.repeat)), sort_keys=True))


if __name__ == "__main__":
    main()
