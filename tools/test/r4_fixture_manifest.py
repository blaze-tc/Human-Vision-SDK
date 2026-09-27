"""Offline, hash-bound R4 video frames and independent detector input goldens.

Only the ignored output directory contains decoded pixels and tensors. This
does not assert that Unity VideoPlayer decodes the same bytes on Android.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np

from topdown_video_reference import preprocess_rgba

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "tests/fixtures/ncnn_prepared_detector_contract.json"
PROFILE = ROOT / "profiles/android-ncnn-vulkan.json"
LOCAL_PACK = ROOT / "out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16"

# Human annotations made after viewing the exact decoded frames, in source
# pixel coordinates. These are deliberately independent of model predictions.
ANNOTATIONS = {
    ("55cd66ae01696939a9c78ab55039e4011c2be4b8449e9e1c893959bc0d1975e0", 150): [
        {"id": "left-black", "bbox_xyxy": [34, 962, 279, 1598], "visibility": "full"},
        {"id": "left-gray", "bbox_xyxy": [241, 977, 461, 1576], "visibility": "full"},
        {"id": "center-black", "bbox_xyxy": [371, 948, 609, 1618], "visibility": "full"},
        {"id": "right-gray", "bbox_xyxy": [574, 985, 772, 1568], "visibility": "full"},
        {"id": "right-black", "bbox_xyxy": [719, 971, 924, 1594], "visibility": "full"},
    ],
    ("6abd4a523e9e0dc9961a3f037e0c33600271ff3a53d170e5f1dbd8c562480f53", 150): [
        {"id": "left-rear-blue", "bbox_xyxy": [449, 472, 562, 825], "visibility": "partly_occluded_by_left_front"},
        {"id": "left-front-pink", "bbox_xyxy": [494, 551, 657, 896], "visibility": "full"},
        {"id": "mid-left-purple", "bbox_xyxy": [686, 545, 783, 831], "visibility": "full"},
        {"id": "foreground-adult", "bbox_xyxy": [843, 405, 1093, 1080], "visibility": "bottom_clipped"},
        {"id": "mid-right-blue", "bbox_xyxy": [1077, 493, 1196, 837], "visibility": "full"},
        {"id": "right-front-blue", "bbox_xyxy": [1378, 540, 1522, 915], "visibility": "full"},
        {"id": "far-right-pink", "bbox_xyxy": [1691, 528, 1819, 828], "visibility": "full"},
    ],
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _decoder_contract() -> dict:
    build = cv2.getBuildInformation().splitlines()
    versions = {}
    for key in ("avcodec", "avformat", "avutil"):
        line = next((line for line in build if line.strip().startswith(key + ":")), "")
        if "(" not in line or ")" not in line: raise ValueError(f"FFmpeg {key} version unavailable")
        versions[key] = line.split("(", 1)[1].split(")", 1)[0]
    return {"backend": "FFMPEG", "opencv": cv2.__version__, **versions}


def _fixture_id(video_hash: str, frame_index: int, rgba_hash: str) -> str:
    return hashlib.sha256(f"{video_hash}:{frame_index}:{rgba_hash}".encode("ascii")).hexdigest()


def _decode_exact(video_path: Path, frame_index: int) -> tuple[np.ndarray, str]:
    if isinstance(frame_index, bool) or not isinstance(frame_index, int) or frame_index < 0:
        raise ValueError("frame index must be a nonnegative integer")
    capture = cv2.VideoCapture(str(video_path), cv2.CAP_FFMPEG)
    if not capture.isOpened() or capture.getBackendName() != "FFMPEG":
        raise ValueError("Pinned FFmpeg decoder unavailable")
    # Decode sequentially: no rounded timestamp or unverified seek position.
    frame = None
    for _ in range(frame_index + 1):
        ok, frame = capture.read()
        if not ok: break
    backend = capture.getBackendName()
    capture.release()
    if not ok or frame is None:
        raise ValueError(f"Cannot decode exact frame index {frame_index}")
    return cv2.cvtColor(frame, cv2.COLOR_BGR2RGBA), backend


def transform_point(x: int, y: int, width: int, height: int,
                    rotation: int, mirror: bool = False) -> tuple[int, int]:
    """Clockwise display rotation, then horizontal front-preview mirror."""
    if width <= 0 or height <= 0 or not (0 <= x < width and 0 <= y < height):
        raise ValueError("point outside source")
    if rotation == 0: tx, ty, out_width = x, y, width
    elif rotation == 90: tx, ty, out_width = height - 1 - y, x, height
    elif rotation == 180: tx, ty, out_width = width - 1 - x, height - 1 - y, width
    elif rotation == 270: tx, ty, out_width = y, width - 1 - x, height
    else: raise ValueError("rotation must be 0, 90, 180, or 270")
    return (out_width - 1 - tx if mirror else tx), ty


def _fp16_rtz_bytes(tensor: np.ndarray) -> bytes:
    if not np.isfinite(tensor).all(): raise ValueError("nonfinite reference tensor")
    nearest = tensor.astype(np.float16)
    # numpy uses nearest-even; the observed Snapdragon packing stage truncates
    # finite FP32 values toward zero. Step back by one half ULP on overshoot.
    overshot = np.abs(nearest.astype(np.float32)) > np.abs(tensor)
    nearest[overshot] = np.nextafter(nearest[overshot], np.float16(0))
    return nearest.astype("<f2", copy=False).tobytes()


def _artifact(output_dir: Path, name: str, data: bytes, shape: list[int], dtype: str) -> dict:
    (output_dir / name).write_bytes(data)
    return {"file": name, "sha256": hashlib.sha256(data).hexdigest(),
            "shape": shape, "dtype": dtype, "byte_length": len(data)}


def build_manifest(video_path: Path, frame_index: int, output_dir: Path) -> dict:
    video_path, output_dir = Path(video_path).resolve(), Path(output_dir)
    if not video_path.is_file(): raise ValueError("Video file missing")
    output_dir.mkdir(parents=True, exist_ok=True)
    video_hash = sha256_file(video_path)
    rgba, backend = _decode_exact(video_path, frame_index)
    h, w, channels = rgba.shape
    if channels != 4 or not rgba.flags.c_contiguous: raise ValueError("Decoder RGBA layout invalid")
    fp32 = preprocess_rgba(rgba).astype("<f4", copy=False)
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    if (contract["width"], contract["height"], contract["elempack"]) != (320, 320, 1):
        raise ValueError("Pinned detector contract changed")
    if LOCAL_PACK.is_dir():
        for key, filename in (("param_sha256", "model.param"), ("bin_sha256", "model.bin")):
            if sha256_file(LOCAL_PACK / "detector" / filename) != contract[key]:
                raise ValueError(f"Local detector {filename} hash mismatch")
    artifacts = {
        "rgba": _artifact(output_dir, "source.rgba", rgba.tobytes(), [h, w, 4], "uint8"),
        "tensor_fp32": _artifact(output_dir, "detector-rgb-chw.f32", fp32.tobytes(),
                                 [1, 3, 320, 320], "float32-le"),
        "tensor_fp16_rtz": _artifact(output_dir, "detector-rgb-chw-rtz.f16",
                                     _fp16_rtz_bytes(fp32), [1, 3, 320, 320], "float16-le"),
    }
    scale = min(320 / w, 320 / h)
    sw, sh = round(w * scale), round(h * scale)
    manifest = {
        "schema_version": 1, "video_path": str(video_path), "video_sha256": video_hash,
        "frame_index": frame_index, "decoder": _decoder_contract(),
        "fixture_id": _fixture_id(video_hash, frame_index, artifacts["rgba"]["sha256"]),
        "width": w, "height": h, "row_stride": w * 4,
        "pixel_contract": {"format": "rgba8-unorm", "rgb_range": "full_0_255",
                           "color_space": "decoded_srgb_assumed", "alpha": "opaque_255",
                           "orientation": "top_left_rows"},
        "transforms": {"rotation_clockwise_degrees": [0, 90, 180, 270],
                       "front_preview_horizontal_mirror": [False, True],
                       "order": "rotate_then_mirror", "source_orientation_degrees": 0},
        "preprocessing": {"method": "OpenCV INTER_LINEAR letterbox RGB114",
                          "output_width": 320, "output_height": 320,
                          "resized_width": sw, "resized_height": sh,
                          "pad_left": (320 - sw) // 2, "pad_top": (320 - sh) // 2,
                          "mean": contract["mean"], "norm": contract["norm"],
                          "channel_order": "RGB", "tensor_layout": "NCHW",
                          "packing": "planar_elempack1", "fp16_conversion": "FP32_to_FP16_RTZ"},
        "model": {"modelpack": contract["modelpack"], "detector_param_sha256": contract["param_sha256"],
                  "detector_bin_sha256": contract["bin_sha256"],
                  "input_contract_sha256": sha256_file(CONTRACT),
                  "profile_sha256": sha256_file(PROFILE)},
        "artifacts": artifacts,
        "annotations": ANNOTATIONS.get((video_hash, frame_index), []),
    }
    validate_manifest(manifest, output_dir)
    return manifest


def validate_manifest(manifest: dict, root: Path) -> None:
    try:
        if manifest["schema_version"] != 1: raise ValueError("schema version")
        if not manifest["decoder"]["backend"] or not manifest["decoder"]["opencv"]:
            raise ValueError("decoder missing")
        if manifest["decoder"] != _decoder_contract():
            raise ValueError("decoder version mismatch")
        video_path = Path(manifest["video_path"])
        if sha256_file(video_path) != manifest["video_sha256"]: raise ValueError("video SHA mismatch")
        rgba, _ = _decode_exact(video_path, manifest["frame_index"])
        h, w = rgba.shape[:2]
        if (manifest["width"], manifest["height"], manifest["row_stride"]) != (w, h, w * 4):
            raise ValueError("source shape/stride mismatch")
        if manifest["pixel_contract"] != {"format": "rgba8-unorm", "rgb_range": "full_0_255",
                                          "color_space": "decoded_srgb_assumed", "alpha": "opaque_255",
                                          "orientation": "top_left_rows"}:
            raise ValueError("pixel contract mismatch")
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        if manifest["model"] != {"modelpack": contract["modelpack"],
                                "detector_param_sha256": contract["param_sha256"],
                                "detector_bin_sha256": contract["bin_sha256"],
                                "input_contract_sha256": sha256_file(CONTRACT),
                                "profile_sha256": sha256_file(PROFILE)}:
            raise ValueError("model/profile hash mismatch")
        scale = min(320 / w, 320 / h); sw, sh = round(w * scale), round(h * scale)
        expected_pre = {"method": "OpenCV INTER_LINEAR letterbox RGB114", "output_width": 320,
                        "output_height": 320, "resized_width": sw, "resized_height": sh,
                        "pad_left": (320 - sw) // 2, "pad_top": (320 - sh) // 2,
                        "mean": contract["mean"], "norm": contract["norm"],
                        "channel_order": "RGB", "tensor_layout": "NCHW",
                        "packing": "planar_elempack1", "fp16_conversion": "FP32_to_FP16_RTZ"}
        if manifest["preprocessing"] != expected_pre: raise ValueError("preprocessing mismatch")
        if manifest["transforms"] != {"rotation_clockwise_degrees": [0, 90, 180, 270],
                                     "front_preview_horizontal_mirror": [False, True],
                                     "order": "rotate_then_mirror", "source_orientation_degrees": 0}:
            raise ValueError("transform contract mismatch")
        expected_shapes = {"rgba": ([h, w, 4], "uint8", rgba.tobytes()),
                           "tensor_fp32": ([1, 3, 320, 320], "float32-le", None),
                           "tensor_fp16_rtz": ([1, 3, 320, 320], "float16-le", None)}
        fp32 = preprocess_rgba(rgba).astype("<f4", copy=False)
        expected_shapes["tensor_fp32"] = ([1, 3, 320, 320], "float32-le", fp32.tobytes())
        expected_shapes["tensor_fp16_rtz"] = ([1, 3, 320, 320], "float16-le", _fp16_rtz_bytes(fp32))
        if set(manifest["artifacts"]) != set(expected_shapes): raise ValueError("artifact set mismatch")
        if manifest["fixture_id"] != _fixture_id(manifest["video_sha256"],
                                                   manifest["frame_index"],
                                                   manifest["artifacts"]["rgba"]["sha256"]):
            raise ValueError("fixture identity mismatch")
        for key, (shape, dtype, expected) in expected_shapes.items():
            entry = manifest["artifacts"][key]
            if entry["shape"] != shape or entry["dtype"] != dtype or entry["byte_length"] != len(expected):
                raise ValueError(f"{key} shape/stride mismatch")
            name = entry["file"]
            if name != {"rgba": "source.rgba", "tensor_fp32": "detector-rgb-chw.f32",
                        "tensor_fp16_rtz": "detector-rgb-chw-rtz.f16"}[key]:
                raise ValueError(f"{key} path mismatch")
            data = (Path(root) / name).read_bytes()
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise ValueError(f"{key} SHA mismatch")
            if key != "rgba" and not np.isfinite(np.frombuffer(data, dtype="<f4" if key == "tensor_fp32" else "<f2")).all():
                raise ValueError(f"{key} nonfinite tensor")
            if data != expected:
                raise ValueError("frame identity mismatch" if key == "rgba" else f"{key} reference mismatch")
        if not np.all(rgba[:, :, 3] == 255): raise ValueError("nonopaque decoded alpha")
        for person in manifest["annotations"]:
            x0, y0, x1, y1 = person["bbox_xyxy"]
            if not (person["id"] and 0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h
                    and person["visibility"] in ("full", "partly_occluded_by_left_front", "bottom_clipped")):
                raise ValueError("person annotation invalid")
        pinned = ANNOTATIONS.get((manifest["video_sha256"], manifest["frame_index"]))
        if pinned is not None and manifest["annotations"] != pinned:
            raise ValueError("pinned person annotations mismatch")
    except (KeyError, TypeError, FileNotFoundError, OSError) as error:
        raise ValueError(f"manifest incomplete: {error}") from error


def _analytic_source(name: str) -> np.ndarray:
    if name == "asymmetric_corners":
        image = np.full((3, 5, 4), (31, 47, 59, 255), dtype=np.uint8)
        for (x, y), color in {
            (0, 0): (255, 0, 0, 255), (4, 0): (0, 255, 0, 255),
            (0, 2): (0, 0, 255, 255), (4, 2): (240, 200, 40, 255),
            (1, 2): (18, 230, 170, 255),
        }.items(): image[y, x] = color
        return image
    if name == "non_square_grid":
        image = np.empty((4, 7, 4), dtype=np.uint8)
        for y in range(4):
            for x in range(7): image[y, x] = (17 + 27 * x, 11 + 53 * y,
                                               23 + 5 * x + 31 * y, 255)
        return image
    raise ValueError("unknown analytic fixture")


def _rotate_mirror(image: np.ndarray, rotation: int, mirror: bool) -> np.ndarray:
    result = np.rot90(image, -(rotation // 90))
    if mirror: result = np.fliplr(result)
    return np.ascontiguousarray(result)


def _analytic_landmarks(source: np.ndarray, rotation: int, mirror: bool) -> dict:
    h, w = source.shape[:2]
    positions = {"TL": (0, 0), "TR": (w - 1, 0), "BL": (0, h - 1),
                 "BR": (w - 1, h - 1), "P": (1, h - 1)}
    return {name: {"source_xy": [x, y],
                   "output_xy": list(transform_point(x, y, w, h, rotation, mirror)),
                   "rgba": source[y, x].tolist()}
            for name, (x, y) in positions.items()}


def build_analytic_fixtures(output_dir: Path) -> dict:
    """Generate uploadable asymmetric source and tensor goldens for all 8 transforms."""
    output_dir = Path(output_dir); output_dir.mkdir(parents=True, exist_ok=True)
    cases = []
    for fixture in ("asymmetric_corners", "non_square_grid"):
        source = _analytic_source(fixture)
        for rotation in (0, 90, 180, 270):
            for mirror in (False, True):
                label = f"{fixture}-r{rotation}-m{int(mirror)}"
                case_dir = output_dir / label; case_dir.mkdir(parents=True, exist_ok=True)
                rgba = _rotate_mirror(source, rotation, mirror)
                fp32 = preprocess_rgba(rgba).astype("<f4", copy=False)
                h, w = rgba.shape[:2]
                cases.append({
                    "fixture": fixture, "rotation": rotation, "mirror": mirror,
                    "folder": label, "source_width": source.shape[1],
                    "source_height": source.shape[0], "source_sha256": hashlib.sha256(source.tobytes()).hexdigest(),
                    "width": w, "height": h, "row_stride": w * 4,
                    "landmarks": _analytic_landmarks(source, rotation, mirror),
                    "artifacts": {
                        "rgba": _artifact(case_dir, "source.rgba", rgba.tobytes(), [h, w, 4], "uint8"),
                        "tensor_fp32": _artifact(case_dir, "detector-rgb-chw.f32", fp32.tobytes(),
                                                 [1, 3, 320, 320], "float32-le"),
                        "tensor_fp16_rtz": _artifact(case_dir, "detector-rgb-chw-rtz.f16",
                                                     _fp16_rtz_bytes(fp32), [1, 3, 320, 320], "float16-le"),
                    },
                })
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    manifest = {"schema_version": 1, "kind": "analytic_gpu_uploads",
                "pixel_contract": {"format": "rgba8-unorm", "rgb_range": "full_0_255",
                                   "color_space": "synthetic_srgb", "alpha": "opaque_255",
                                   "orientation": "top_left_rows"},
                "preprocessing": {"method": "OpenCV INTER_LINEAR letterbox RGB114",
                                  "mean": contract["mean"], "norm": contract["norm"],
                                  "tensor_layout": "NCHW", "packing": "planar_elempack1",
                                  "fp16_conversion": "FP32_to_FP16_RTZ"},
                "model": {"input_contract_sha256": sha256_file(CONTRACT),
                          "detector_param_sha256": contract["param_sha256"],
                          "detector_bin_sha256": contract["bin_sha256"],
                          "profile_sha256": sha256_file(PROFILE)}, "cases": cases}
    validate_analytic_fixtures(manifest, output_dir)
    return manifest


def validate_analytic_fixtures(manifest: dict, root: Path) -> None:
    try:
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        if manifest["schema_version"] != 1 or manifest["kind"] != "analytic_gpu_uploads":
            raise ValueError("analytic schema mismatch")
        if manifest["pixel_contract"] != {"format": "rgba8-unorm", "rgb_range": "full_0_255",
                                           "color_space": "synthetic_srgb", "alpha": "opaque_255",
                                           "orientation": "top_left_rows"}:
            raise ValueError("analytic pixel contract mismatch")
        if manifest["preprocessing"] != {"method": "OpenCV INTER_LINEAR letterbox RGB114",
                                          "mean": contract["mean"], "norm": contract["norm"],
                                          "tensor_layout": "NCHW", "packing": "planar_elempack1",
                                          "fp16_conversion": "FP32_to_FP16_RTZ"}:
            raise ValueError("analytic preprocessing mismatch")
        if manifest["model"] != {"input_contract_sha256": sha256_file(CONTRACT),
                                "detector_param_sha256": contract["param_sha256"],
                                "detector_bin_sha256": contract["bin_sha256"],
                                "profile_sha256": sha256_file(PROFILE)}:
            raise ValueError("analytic model/profile mismatch")
        expected_keys = {(fixture, rotation, mirror)
                         for fixture in ("asymmetric_corners", "non_square_grid")
                         for rotation in (0, 90, 180, 270) for mirror in (False, True)}
        actual_keys = {(case["fixture"], case["rotation"], case["mirror"])
                       for case in manifest["cases"]}
        if len(manifest["cases"]) != 16 or actual_keys != expected_keys:
            raise ValueError("analytic transform coverage mismatch")
        for case in manifest["cases"]:
            source = _analytic_source(case["fixture"])
            expected_folder = f"{case['fixture']}-r{case['rotation']}-m{int(case['mirror'])}"
            if case["folder"] != expected_folder: raise ValueError("analytic folder mismatch")
            rgba = _rotate_mirror(source, case["rotation"], case["mirror"])
            h, w = rgba.shape[:2]
            if (case["source_width"], case["source_height"], case["width"],
                    case["height"], case["row_stride"]) != (source.shape[1], source.shape[0], w, h, w * 4):
                raise ValueError("analytic shape/stride mismatch")
            if case["source_sha256"] != hashlib.sha256(source.tobytes()).hexdigest():
                raise ValueError("analytic source SHA mismatch")
            if case["landmarks"] != _analytic_landmarks(source, case["rotation"], case["mirror"]):
                raise ValueError("analytic landmark mismatch")
            fp32 = preprocess_rgba(rgba).astype("<f4", copy=False)
            expected = {"rgba": (rgba.tobytes(), [h, w, 4], "uint8", "source.rgba"),
                        "tensor_fp32": (fp32.tobytes(), [1, 3, 320, 320], "float32-le", "detector-rgb-chw.f32"),
                        "tensor_fp16_rtz": (_fp16_rtz_bytes(fp32), [1, 3, 320, 320],
                                            "float16-le", "detector-rgb-chw-rtz.f16")}
            if set(case["artifacts"]) != set(expected): raise ValueError("analytic artifact set mismatch")
            for key, (data, shape, dtype, filename) in expected.items():
                entry = case["artifacts"][key]
                if entry != {"file": filename, "sha256": hashlib.sha256(data).hexdigest(),
                             "shape": shape, "dtype": dtype, "byte_length": len(data)}:
                    raise ValueError(f"analytic {key} contract mismatch")
                if (Path(root) / expected_folder / filename).read_bytes() != data:
                    raise ValueError(f"analytic {key} bytes mismatch")
    except (KeyError, TypeError, FileNotFoundError, OSError) as error:
        raise ValueError(f"analytic manifest incomplete: {error}") from error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--video", type=Path)
    source.add_argument("--analytic", action="store_true")
    parser.add_argument("--frame-index", type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    if args.analytic:
        if args.frame_index is not None: parser.error("--frame-index is video-only")
        manifest = build_analytic_fixtures(args.output_dir)
    else:
        if args.frame_index is None: parser.error("--frame-index is required with --video")
        manifest = build_manifest(args.video, args.frame_index, args.output_dir)
    path = args.output_dir / "manifest.json"
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.analytic:
        print(json.dumps({"manifest": str(path), "sha256": sha256_file(path),
                          "cases": len(manifest["cases"])}))
    else:
        print(json.dumps({"manifest": str(path), "sha256": sha256_file(path),
                          "frame_index": manifest["frame_index"],
                          "annotations": len(manifest["annotations"]),
                          "artifacts": {k: v["sha256"] for k, v in manifest["artifacts"].items()}}))


if __name__ == "__main__": main()
