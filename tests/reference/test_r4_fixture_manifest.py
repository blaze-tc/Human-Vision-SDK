"""R4 offline fixture identity, byte integrity, and geometry contracts."""

import copy
import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/test"))
from r4_fixture_manifest import (build_analytic_fixtures, build_manifest,  # noqa: E402
                                 transform_point, validate_analytic_fixtures,
                                 validate_manifest)
from generate_prepared_gate_golden import f32_to_fp16_rtz_bits  # noqa: E402


class R4FixtureManifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.video = self.root / "source.avi"
        writer = cv2.VideoWriter(str(self.video), cv2.VideoWriter_fourcc(*"MJPG"), 5, (12, 8))
        self.assertTrue(writer.isOpened())
        for index in range(3):
            frame = np.zeros((8, 12, 3), np.uint8)
            frame[:, :, :] = (index * 35, 40, 200)
            frame[1:4, index: index + 3] = (10, 210, 20)
            writer.write(frame)
        writer.release()
        self.output = self.root / "fixture"
        self.manifest = build_manifest(self.video, 1, self.output)

    def test_identical_extraction_has_identical_bytes_and_hashes(self):
        other = self.root / "other"
        repeated = build_manifest(self.video, 1, other)
        self.assertEqual(self.manifest, repeated)
        for key in ("rgba", "tensor_fp32", "tensor_fp16_rtz"):
            name = self.manifest["artifacts"][key]["file"]
            self.assertEqual((self.output / name).read_bytes(), (other / name).read_bytes())
        validate_manifest(self.manifest, self.output)

    def test_decoder_library_versions_and_fixture_identity_are_bound(self):
        self.assertIn("avcodec", self.manifest["decoder"])
        changed = copy.deepcopy(self.manifest)
        changed["fixture_id"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "fixture identity"):
            validate_manifest(changed, self.output)

    def test_independent_pad_and_channel_oracle(self):
        tensor = np.frombuffer((self.output / "detector-rgb-chw.f32").read_bytes(),
                               dtype="<f4").reshape(1, 3, 320, 320)
        rgba = np.frombuffer((self.output / "source.rgba").read_bytes(),
                             dtype=np.uint8).reshape(8, 12, 4)
        mean = (123.675, 116.28, 103.53)
        norm = (0.017124753831663668, 0.01750700280112045, 0.017429193899782137)
        # 12x8 -> 320x213, top pad 53; (160,160) lands inside source.
        self.assertEqual(self.manifest["preprocessing"]["pad_top"], 53)
        for channel in range(3):
            pad = np.float32((np.float32(114) - np.float32(mean[channel])) * np.float32(norm[channel]))
            self.assertEqual(tensor[0, channel, 0, 0], pad)
            # Avoid interpolation: the source has uniform rows away from the small marker.
            source = np.float32(rgba[7, 11, channel])
            observed = tensor[0, channel, 265, 319]
            self.assertAlmostEqual(float(observed), float((source - np.float32(mean[channel])) * np.float32(norm[channel])), places=5)

        half = np.frombuffer((self.output / "detector-rgb-chw-rtz.f16").read_bytes(), dtype="<u2")
        values = tensor.reshape(-1)
        for index in (0, 100, 320 * 160 + 160, 320 * 320, len(values) - 1):
            self.assertEqual(int(half[index]), f32_to_fp16_rtz_bits(float(values[index])))

    def test_altered_source_byte_rejects(self):
        entry = self.manifest["artifacts"]["rgba"]
        path = self.output / entry["file"]
        data = bytearray(path.read_bytes()); data[4] ^= 1; path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "rgba.*SHA"):
            validate_manifest(self.manifest, self.output)

    def test_wrong_frame_index_rejects_even_when_pixels_are_intact(self):
        changed = copy.deepcopy(self.manifest); changed["frame_index"] = 2
        with self.assertRaisesRegex(ValueError, "(frame|fixture) identity"):
            validate_manifest(changed, self.output)

    def test_missing_decoder_and_wrong_shape_stride_reject(self):
        for mutate in (
            lambda m: m.pop("decoder"),
            lambda m: m.update(width=13),
            lambda m: m.update(row_stride=47),
            lambda m: m["artifacts"]["tensor_fp32"].update(shape=[1, 3, 319, 320]),
        ):
            changed = copy.deepcopy(self.manifest); mutate(changed)
            with self.assertRaises(ValueError): validate_manifest(changed, self.output)

    def test_nonfinite_tensor_rejects_even_with_updated_sha(self):
        changed = copy.deepcopy(self.manifest)
        entry = changed["artifacts"]["tensor_fp32"]
        path = self.output / entry["file"]
        data = bytearray(path.read_bytes()); data[:4] = np.float32(np.nan).tobytes()
        path.write_bytes(data); entry["sha256"] = hashlib.sha256(data).hexdigest()
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            validate_manifest(changed, self.output)

    def test_asymmetric_grid_all_rotations_and_mirror_preserve_locations(self):
        # Distinct corners on a 5x3 grid also catch swapped width/height.
        points = {(0, 0): "TL", (4, 0): "TR", (0, 2): "BL", (4, 2): "BR", (1, 2): "P"}
        for rotation in (0, 90, 180, 270):
            for mirror in (False, True):
                mapped = {transform_point(x, y, 5, 3, rotation, mirror): label
                          for (x, y), label in points.items()}
                self.assertEqual(len(mapped), len(points))
                out_w, out_h = (3, 5) if rotation in (90, 270) else (5, 3)
                self.assertTrue(all(0 <= x < out_w and 0 <= y < out_h for x, y in mapped))
                for (x, y), label in points.items():
                    self.assertEqual(mapped[transform_point(x, y, 5, 3, rotation, mirror)], label)
        self.assertEqual(transform_point(0, 0, 5, 3, 90, False), (2, 0))
        self.assertEqual(transform_point(0, 0, 5, 3, 90, True), (0, 0))
        self.assertEqual(transform_point(1, 2, 5, 3, 270, False), (2, 3))
        self.assertEqual(transform_point(4, 0, 5, 3, 180, False), (0, 2))
        self.assertEqual(transform_point(0, 2, 5, 3, 270, True), (0, 4))

    def test_independent_expected_coordinates_for_all_eight_transforms(self):
        # Hand-derived coordinates, rather than asking transform_point to
        # verify its own output through a round trip.
        cases = {
            (0, False): ((0, 0), (4, 0), (1, 2)),
            (0, True): ((4, 0), (0, 0), (3, 2)),
            (90, False): ((2, 0), (2, 4), (0, 1)),
            (90, True): ((0, 0), (0, 4), (2, 1)),
            (180, False): ((4, 2), (0, 2), (3, 0)),
            (180, True): ((0, 2), (4, 2), (1, 0)),
            (270, False): ((0, 4), (0, 0), (2, 3)),
            (270, True): ((2, 4), (2, 0), (0, 3)),
        }
        for (rotation, mirror), expected in cases.items():
            self.assertEqual(tuple(transform_point(x, y, 5, 3, rotation, mirror)
                                   for x, y in ((0, 0), (4, 0), (1, 2))), expected)

    def test_reusable_analytic_pixels_and_tensors_for_all_transforms(self):
        output = self.root / "analytic"
        manifest = build_analytic_fixtures(output)
        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(len(manifest["cases"]), 16)
        validate_analytic_fixtures(manifest, output)
        for case in manifest["cases"]:
            self.assertEqual(set(case["artifacts"]), {"rgba", "tensor_fp32", "tensor_fp16_rtz"})
            self.assertEqual(len(case["landmarks"]), 5)
            self.assertEqual(case["landmarks"]["TL"]["source_xy"], [0, 0])
            self.assertEqual(case["landmarks"]["TL"]["output_xy"],
                             list(transform_point(0, 0, case["source_width"], case["source_height"],
                                                  case["rotation"], case["mirror"])))
            self.assertEqual(case["artifacts"]["rgba"]["shape"][:2],
                             [3, 5] if case["fixture"] == "asymmetric_corners" and case["rotation"] in (0, 180)
                             else [5, 3] if case["fixture"] == "asymmetric_corners"
                             else [4, 7] if case["rotation"] in (0, 180) else [7, 4])
        again = self.root / "analytic-again"
        self.assertEqual(manifest, build_analytic_fixtures(again))
        changed = copy.deepcopy(manifest)
        changed["cases"][0]["artifacts"]["rgba"]["sha256"] = "0" * 64
        with self.assertRaises(ValueError): validate_analytic_fixtures(changed, output)

    def test_saved_rgba_pixels_match_independent_color_and_coordinate_oracle(self):
        output = self.root / "pixel-oracle"
        manifest = build_analytic_fixtures(output)
        # Each tuple is TL, TR, BL, BR, P. These locations are hand-derived
        # for the two source geometries; this test never calls transform_point,
        # _rotate_mirror, or _analytic_source for its expected output.
        locations = {
            "asymmetric_corners": {
                (0, False): ((0, 0), (4, 0), (0, 2), (4, 2), (1, 2)),
                (0, True): ((4, 0), (0, 0), (4, 2), (0, 2), (3, 2)),
                (90, False): ((2, 0), (2, 4), (0, 0), (0, 4), (0, 1)),
                (90, True): ((0, 0), (0, 4), (2, 0), (2, 4), (2, 1)),
                (180, False): ((4, 2), (0, 2), (4, 0), (0, 0), (3, 0)),
                (180, True): ((0, 2), (4, 2), (0, 0), (4, 0), (1, 0)),
                (270, False): ((0, 4), (0, 0), (2, 4), (2, 0), (2, 3)),
                (270, True): ((2, 4), (2, 0), (0, 4), (0, 0), (0, 3)),
            },
            "non_square_grid": {
                (0, False): ((0, 0), (6, 0), (0, 3), (6, 3), (1, 3)),
                (0, True): ((6, 0), (0, 0), (6, 3), (0, 3), (5, 3)),
                (90, False): ((3, 0), (3, 6), (0, 0), (0, 6), (0, 1)),
                (90, True): ((0, 0), (0, 6), (3, 0), (3, 6), (3, 1)),
                (180, False): ((6, 3), (0, 3), (6, 0), (0, 0), (5, 0)),
                (180, True): ((0, 3), (6, 3), (0, 0), (6, 0), (1, 0)),
                (270, False): ((0, 6), (0, 0), (3, 6), (3, 0), (3, 5)),
                (270, True): ((3, 6), (3, 0), (0, 6), (0, 0), (0, 5)),
            },
        }
        colors = {
            "asymmetric_corners": ((255, 0, 0, 255), (0, 255, 0, 255),
                                   (0, 0, 255, 255), (240, 200, 40, 255),
                                   (18, 230, 170, 255)),
            "non_square_grid": ((17, 11, 23, 255), (179, 11, 53, 255),
                                (17, 170, 116, 255), (179, 170, 146, 255),
                                (44, 170, 121, 255)),
        }
        for case in manifest["cases"]:
            key = (case["rotation"], case["mirror"])
            rgba = np.frombuffer((output / case["folder"] / "source.rgba").read_bytes(),
                                 dtype=np.uint8).reshape(case["height"], case["width"], 4)
            for name, (x, y), color in zip(("TL", "TR", "BL", "BR", "P"),
                                            locations[case["fixture"]][key], colors[case["fixture"]]):
                with self.subTest(fixture=case["fixture"], transform=key, point=name):
                    self.assertEqual(tuple(rgba[y, x]), color)
                    self.assertEqual(case["landmarks"][name]["output_xy"], [x, y])
                    self.assertEqual(case["landmarks"][name]["rgba"], list(color))


if __name__ == "__main__": unittest.main()
