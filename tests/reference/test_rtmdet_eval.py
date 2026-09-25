"""Refuse unpinned or silently altered local RTMDet evaluation assets."""

import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import hashlib

from tools.models.ncnn.finalize_rtmdet_eval import corrected_rtmdet_param, finalize


RESHAPES = (1600, 400, 100, 1600, 400, 100)


def raw_graph():
    lines = ["7767517", "316 345", "Input in0 0 1 in0"]
    lines += [f"Reshape /Reshape{'_' + str(i) if i else ''} 1 1 bottom{i} top{i} 0={1 if i < 3 else 4} 1=-1"
              for i in range(6)]
    lines += [f"ReLU layer{i} 1 1 a{i} b{i}" for i in range(309)]
    return "\n".join(lines) + "\n"


class RtmdetEvalTests(unittest.TestCase):
    def test_only_seven_audited_dimension_fields_change(self):
        raw = raw_graph()
        fixed = corrected_rtmdet_param(raw)
        before, after = raw.splitlines(), fixed.splitlines()
        self.assertEqual(len(before), len(after))
        self.assertEqual(after[2], "Input in0 0 1 in0 0=320 1=320 2=3")
        for i, dimension in enumerate(RESHAPES, 3):
            self.assertEqual(after[i], before[i].replace("1=-1", f"1={dimension}"))
        self.assertEqual(after[9:], before[9:])

    def test_each_mutated_raw_dimension_is_rejected(self):
        raw = raw_graph()
        for i in range(3, 9):
            with self.subTest(line=i):
                lines = raw.splitlines()
                lines[i] = lines[i].replace("1=-1", "1=77")
                with self.assertRaises(ValueError):
                    corrected_rtmdet_param("\n".join(lines) + "\n")
        with self.assertRaises(ValueError):
            corrected_rtmdet_param(raw.replace("Input in0 0 1 in0", "Input in0 0 1 in0 0=321"))

    def test_missing_or_extra_graph_nodes_are_rejected(self):
        raw = raw_graph()
        for changed in (raw.replace("316 345", "315 345"),
                        raw.replace("Reshape /Reshape_5", "Reshape /Reshape_6"),
                        raw.replace("Input in0", "Input other")):
            with self.assertRaises(ValueError):
                corrected_rtmdet_param(changed)

    def test_finalizer_rejects_wrong_checkpoint(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "checkpoint").write_bytes(b"wrong")
            with self.assertRaisesRegex(ValueError, "checkpoint SHA-256 mismatch"):
                finalize(root / "checkpoint", root / "onnx", root / "raw.param",
                         root / "raw.bin", root / "output")

    def test_finalizer_rejects_blob_pack_and_color_contract_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("checkpoint", "onnx", "raw.param", "raw.bin"):
                (root / name).write_bytes(b"fixture")
            (root / "raw.param").write_text(raw_graph(), encoding="utf-8")
            corrected = corrected_rtmdet_param(raw_graph()).replace("\n", "\r\n")
            digest = hashlib.sha256(corrected.encode()).hexdigest()
            with patch("tools.models.ncnn.finalize_rtmdet_eval.require_hash"), \
                 patch("tools.models.ncnn.finalize_rtmdet_eval.FINAL_PARAM_SHA256", digest):
                for override in ({"input_blob": "other"}, {"elempack": 4},
                                 {"tensor_dtype": "fp32"}, {"color_order": "bgr"},
                                 {"width": 321}, {"height": 321}):
                    with self.subTest(override=override), self.assertRaisesRegex(ValueError, "FP16 pack1"):
                        finalize(root / "checkpoint", root / "onnx", root / "raw.param",
                                 root / "raw.bin", root / "output", input_override=override)


if __name__ == "__main__":
    unittest.main()
