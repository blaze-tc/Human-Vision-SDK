"""Offline experiment contract; does not change the SDK's COCO-17 ABI."""

from __future__ import annotations

import numpy as np


def validate_simcc(x: np.ndarray, y: np.ndarray) -> None:
    if (x.ndim != 3 or y.ndim != 3 or x.shape[0] < 1 or
            x.shape[0] != y.shape[0] or x.shape[1:] != (133, 384) or
            y.shape[1:] != (133, 512)):
        raise ValueError("Expected matched [N,133,384]/[N,133,512] wholebody SimCC outputs")
    if x.dtype != np.float32 or y.dtype != np.float32:
        raise ValueError("Expected float32 wholebody reference outputs")
    if not np.isfinite(x).all() or not np.isfinite(y).all():
        raise ValueError("Wholebody output contains nonfinite values")


def semantic_endpoints(points: np.ndarray, scores: np.ndarray, threshold: float) -> dict:
    """Return documented semantic samples, suppressing invalid hand constituents.

    Handtip uses the middle fingertip, foot uses the big toe, and palm uses
    the mean of hand root and four MCP landmarks. These are explicit adapter
    conventions, not claims of exact Kinect anatomical equivalence.
    """
    if points.shape != (133, 2) or scores.shape != (133,):
        raise ValueError("Expected 133 source-space points and scores")
    if not np.isfinite(points).all() or not np.isfinite(scores).all():
        raise ValueError("Decoded points/scores must be finite")
    if not np.isfinite(threshold) or threshold < 0:
        raise ValueError("Confidence threshold must be finite and nonnegative")

    mappings = {
        "ThumbLeft": [95], "ThumbRight": [116],
        "HandtipLeft": [103], "HandtipRight": [124],
        "HandLeft": [91, 96, 100, 104, 108],
        "HandRight": [112, 117, 121, 125, 129],
        "FootLeft": [17], "FootRight": [20],
    }
    result = {}
    for name, indices in mappings.items():
        confidence = float(scores[indices].min())
        valid = confidence > 0 and confidence >= threshold
        result[name] = {
            "source_indices": indices,
            "kind": "model" if len(indices) == 1 else "derived_from_hand_model",
            "confidence": confidence,
            "valid": valid,
            "pixel": points[indices].mean(axis=0).tolist() if valid else None,
        }
    return result
