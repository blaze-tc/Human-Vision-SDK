"""C2 failure diagnostic: RTMDet golden passed but complete P95 exited.

The inference outputs live in ignored ``out/c2-detector/golden`` because the
large checkpoint and ONNX reference are not distributable test fixtures.
Missing evidence is a failure, never a synthetic pass or a silent skip.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.models.ncnn.model_contract import (
    DETECTOR_CHECKPOINT_SHA256,
    require_hash,
    sha256_file,
)


ROOT = Path(__file__).resolve().parents[3]
PRODUCTION_DETECTOR = ROOT / "modelpacks/precision-t-26-ncnn-fp16/detector"
DETECTOR = ROOT / "out/c2-detector/failure-pack"
EVIDENCE = ROOT / "out/c2-detector"
CHECKPOINT = ROOT / "out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth"
ONNX = EVIDENCE / "rtmdet-nano.onnx"
EXIT_LOG_HASHES = (
    "3fd5f5561b4777dbbad0b707eafd485a658ba23281078ec936074506968cbe0a",
    "5e94f63fa9a54e9079cfb846827cb85a0343c5e8006daa8b8996ca9881773646",
    "cf62c47a47c0cbe8072c650a81a205fe755954f4277fce30395a7d47ea313be3",
)
NANODET = ROOT / "out/c2-nanodet"
NANODET_HASHES = {
    "nanodet-plus-m_320.onnx": "4f12723cce3d48e47ca92cb925ba74d97a965c069208edca660bbb9f7ce2c610",
    "nanodet-plus-m_320_checkpoint.ckpt": "f4c6080f3ef35a64c3030d559438b436462f227ce29700bca71327203e42dea2",
    "model.param": "d79e18ecd8595081bb29fdf0c790a47fcea96fbf58598350569a92e71e915b72",
    "model.bin": "400bc25cc522b0fc2c1654d810e55f32b73fffbfe44bf680a709f740d4e3b195",
    "person.param": "4b7b4dad50107a348e3f5d7b1dd9f26947c6c1c53cb86f78b73cc7e806dfd922",
}
NANODET_CROP_LOG_HASHES = (
    "db22455d984a72f99bcd51a7b4adc93da155768ebba14414d5142b9cb6729b82",
    "af7b1c389c3eea054af07a969bd781888fa2cb3124f13ad97204daaeb8e0bd76",
    "f67dcca913b7750d09872a8aa0f4f291f45e27205c5e52e8c5c5fb5265283673",
)
EXPECTED_COUNTS = {"official": 1, "one-person": 1, "two-people-2": 2,
                   "negative-street": 0}
CONVERTER_TOOLS = {
    "onnx2ncnn": (ROOT / "out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe",
                  "b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb"),
    "ncnnoptimize": (ROOT / "out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe",
                     "40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7"),
}


def verify_converter_tools(tools=CONVERTER_TOOLS) -> None:
    for name, (path, expected_hash) in tools.items():
        require_hash(path, expected_hash, f"{name} executable")


def verify_decoded_json(folder: Path, raw_prefix: str, json_path: Path) -> None:
    import cv2
    import numpy as np

    image = cv2.imread(str(folder / "image.png"))
    if image is None:
        raise ValueError("Missing real golden image")
    height, width = image.shape[:2]
    scale = min(320 / width, 320 / height)
    scaled_width, scaled_height = round(width * scale), round(height * scale)
    left, top = (320 - scaled_width) // 2, (320 - scaled_height) // 2
    centers = np.concatenate([
        np.stack(np.meshgrid(np.arange(size) + .5, np.arange(size) + .5),
                 axis=-1).reshape(-1, 2) * stride
        for size, stride in ((40, 8), (20, 16), (10, 32))
    ], axis=0)
    logits = np.fromfile(folder / f"{raw_prefix}-cls.fp32", np.float32)
    distances = np.fromfile(folder / f"{raw_prefix}-bbox.fp32", np.float32)
    if logits.size != 2100 or distances.size != 8400:
        raise ValueError("Invalid raw detector tensor shape")
    distances = distances.reshape(-1, 4)
    scores = 1 / (1 + np.exp(-logits))
    boxes = np.stack((centers[:, 0] - distances[:, 0],
                      centers[:, 1] - distances[:, 1],
                      centers[:, 0] + distances[:, 2],
                      centers[:, 1] + distances[:, 3]), axis=1)
    boxes[:, 0::2] = (boxes[:, 0::2] - left) / scale
    boxes[:, 1::2] = (boxes[:, 1::2] - top) / scale
    boxes[:, 0::2] = np.clip(boxes[:, 0::2], 0, width)
    boxes[:, 1::2] = np.clip(boxes[:, 1::2], 0, height)
    kept = np.where(scores >= .35)[0]
    kept = kept[np.argsort(-scores[kept])[:1000]]

    def iou(a, b):
        overlap_width = max(0, min(a[2], b[2]) - max(a[0], b[0]))
        overlap_height = max(0, min(a[3], b[3]) - max(a[1], b[1]))
        overlap = overlap_width * overlap_height
        area = lambda box: max(0, box[2] - box[0]) * max(0, box[3] - box[1])
        return overlap / (area(a) + area(b) - overlap + 1e-9)

    selected = []
    for index in kept:
        box = boxes[index]
        if box[2] <= box[0] or box[3] <= box[1]:
            continue
        if any(iou(box, boxes[other]) > .6 for other in selected):
            continue
        selected.append(index)
        if len(selected) == 100:
            break
    decoded = [{"person": True, "score": float(scores[index]),
                "bbox": [float(value) for value in boxes[index]]}
               for index in selected]
    recorded = json.loads(json_path.read_text(encoding="utf-8"))["detections"]
    if decoded != recorded:
        raise ValueError(f"raw detector tensors disagree with JSON: {json_path}")


class RtmdetNcnnGoldenTests(unittest.TestCase):
    def test_tampered_converter_tool_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            forged = Path(directory) / "onnx2ncnn.exe"
            forged.write_bytes(b"forged converter")
            tools = {**CONVERTER_TOOLS, "onnx2ncnn":
                     (forged, CONVERTER_TOOLS["onnx2ncnn"][1])}
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                verify_converter_tools(tools)

    def test_real_graph_and_shared_image_goldens_are_failure_evidence_only(self):
        verify_converter_tools()
        self.assertFalse((PRODUCTION_DETECTOR / "model.json").exists(),
                         "Exited RTMDet must not remain selected for production")
        manifest_path = DETECTOR / "model.json"
        self.assertTrue(manifest_path.is_file(), f"Missing C2 conversion manifest: {manifest_path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(manifest["role"], "detector")
        self.assertEqual(manifest["input_contract"]["elempack"], 1)
        self.assertEqual(manifest["artifacts"]["checkpoint_sha256"], DETECTOR_CHECKPOINT_SHA256)

        param = DETECTOR / "model.param"
        weights = DETECTOR / "model.bin"
        for label, path in (("param", param), ("bin", weights)):
            self.assertTrue(path.is_file(), f"Missing real converted {label}: {path}")
            require_hash(path, manifest["artifacts"][f"{label}_sha256"], label)
        require_hash(CHECKPOINT, DETECTOR_CHECKPOINT_SHA256, "checkpoint")
        require_hash(ONNX, manifest["artifacts"]["onnx_sha256"], "ONNX")

        golden_dir = EVIDENCE / "golden"
        official_image = golden_dir / "official/image.png"
        audit = subprocess.run([
            sys.executable, "-m", "tools.models.ncnn.audit_ncnn_graph",
            "--manifest", str(manifest_path), "--param", str(param),
            "--onnx", str(ONNX), "--checkpoint", str(CHECKPOINT),
            "--bin", str(weights), "--fixture", str(official_image),
        ], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(audit.returncode, 0, audit.stderr)
        index_path = golden_dir / "index.json"
        self.assertTrue(index_path.is_file(), f"Missing actual inference index: {index_path}")
        index = json.loads(index_path.read_text(encoding="utf-8"))
        self.assertEqual(index["detector_id"], "detector.rtmdet.nano.ncnn.fp16")
        self.assertEqual(index["input_elempack"], 1)
        self.assertEqual(index["model_sha256"], sha256_file(weights))
        fixtures = index["fixtures"]
        self.assertEqual({fixture["name"] for fixture in fixtures}, set(EXPECTED_COUNTS))
        seen = set()
        seen_images = set()
        for fixture in fixtures:
            name = fixture["name"]
            with self.subTest(name=name):
                self.assertNotIn(name, seen)
                seen.add(name)
                self.assertNotIn(fixture["image_sha256"], seen_images)
                seen_images.add(fixture["image_sha256"])
                image = golden_dir / name / "image.png"
                self.assertTrue(image.is_file())
                require_hash(image, fixture["image_sha256"], "golden image")
                for raw_name in ("input", "onnx-cls", "onnx-bbox", "ncnn-cls",
                                 "ncnn-bbox", "ncnn-repeat-cls", "ncnn-repeat-bbox"):
                    raw = golden_dir / name / f"{raw_name}.fp32"
                    self.assertTrue(raw.is_file(), f"Missing actual tensor: {raw}")
                    require_hash(raw, fixture[f"{raw_name}_sha256"], raw_name)
                for output in ("cls", "bbox"):
                    self.assertEqual(fixture[f"ncnn-{output}_sha256"],
                                     fixture[f"ncnn-repeat-{output}_sha256"])
                paths = {key: golden_dir / name / f"{key}.json" for key in
                         ("reference", "candidate", "repeat")}
                for key, path in paths.items():
                    self.assertTrue(path.is_file(), f"Missing {key} inference output: {path}")
                    require_hash(path, fixture[f"{key}_sha256"], key)
                for key, raw_prefix in (("reference", "onnx"),
                                        ("candidate", "ncnn"),
                                        ("repeat", "ncnn-repeat")):
                    verify_decoded_json(golden_dir / name, raw_prefix, paths[key])
                    self.assertEqual(len(json.loads(paths[key].read_text(encoding="utf-8"))["detections"]),
                                     EXPECTED_COUNTS[name])
                compare = subprocess.run([
                    sys.executable, "-m", "tools.models.ncnn.compare_detector_outputs",
                    "--manifest", str(golden_dir / name / "model-golden.json"),
                    "--fixture", str(image), "--reference", str(paths["reference"]),
                    "--candidate", str(paths["candidate"]), "--repeat", str(paths["repeat"]),
                    "--checkpoint", str(CHECKPOINT), "--onnx", str(ONNX),
                    "--param", str(param), "--bin", str(weights),
                ], cwd=ROOT, capture_output=True, text=True)
                self.assertEqual(compare.returncode, 0, compare.stderr)

        for run in (1, 2, 3):
            log = EVIDENCE / "bench" / f"full-detector-run{run}.log"
            self.assertTrue(log.is_file(), f"Missing paired device evidence: {log}")
            require_hash(log, EXIT_LOG_HASHES[run - 1], "paired device log")
            rows = [line for line in log.read_text(encoding="utf-8").splitlines()
                    if line and line[0].isdigit() and line.count(",") == 5]
            self.assertEqual(len(rows), 100)
            totals = sorted(float(row.split(",")[4]) for row in rows)
            self.assertGreater(totals[94], 33.33,
                               "RTMDet exit evidence must exceed full detector frame period")

    def test_forged_empty_json_cannot_pass_raw_evidence(self):
        folder = EVIDENCE / "golden/official"
        with tempfile.TemporaryDirectory() as directory:
            forged = Path(directory) / "forged.json"
            forged.write_text('{"detections": []}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "raw.*JSON"):
                verify_decoded_json(folder, "ncnn", forged)

    def test_nanodet_substitution_exited_on_paired_full_detector_p95(self):
        self.assertFalse((PRODUCTION_DETECTOR / "model.param").exists())
        for name, expected in NANODET_HASHES.items():
            require_hash(NANODET / name, expected, name)
        for run, expected in enumerate(NANODET_CROP_LOG_HASHES, 1):
            log = NANODET / "bench" / f"person-run{run}.log"
            require_hash(log, expected, "NanoDet paired device log")
            rows = [line for line in log.read_text(encoding="utf-8").splitlines()
                    if line and line[0].isdigit() and line.count(",") == 5]
            self.assertEqual(len(rows), 100)
            self.assertEqual({int(row.split(",")[5]) for row in rows}, {1})
            totals = sorted(float(row.split(",")[4]) for row in rows)
            self.assertGreater(totals[94], 33.33)


if __name__ == "__main__":
    unittest.main()
