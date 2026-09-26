"""Keep the independent Task 4 source-to-tensor reference reproducible."""

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/test"))
from generate_prepared_gate_golden import EVIDENCE, f32_to_fp16_rtz_bits, generate  # noqa: E402
import json  # noqa: E402


class PreparedGateGoldenTest(unittest.TestCase):
    def test_fp32_to_fp16_truncates_toward_zero_for_both_signs(self):
        # A float32 ULP above one still truncates to one in FP16; a full
        # FP16 ULP advances exactly once. Three quarters of an FP16 ULP
        # would round up under nearest-even, for either sign.
        for sign, sign_bit in ((1, 0), (-1, 0x8000)):
            self.assertEqual(f32_to_fp16_rtz_bits(sign * 1.0), sign_bit | 0x3c00)
            self.assertEqual(f32_to_fp16_rtz_bits(sign * (1.0 + 2**-23)),
                             sign_bit | 0x3c00)
            self.assertEqual(f32_to_fp16_rtz_bits(sign * (1.0 + 3 * 2**-12)),
                             sign_bit | 0x3c00)
            self.assertEqual(f32_to_fp16_rtz_bits(sign * (1.0 + 2**-10)),
                             sign_bit | 0x3c01)

    def test_pinned_source_tensor_and_model_contract(self):
        actual = generate()
        expected = json.loads(EVIDENCE.read_text(encoding="utf-8"))
        self.assertEqual(actual, expected)
        self.assertEqual(actual["fixture_rgba_sha256"],
                         "3e1edfd04bd3abd1b260cc67b07b781e365a85559365032185cd8ec72545ce74")
        self.assertEqual(actual["tensor_fp16_sha256"],
                         "9e598ea5d37ebe348bd8235c412d404d62ceb78018dfdfb942a88a26ad118d19")
        self.assertEqual(actual["tensor_conversion"],
                         "FP32 to FP16 round toward zero (RTZ)")
        self.assertIn("other devices remain device-gated", actual["conversion_scope"])
        self.assertEqual(actual["output_max_logical_bytes"], {"cls": 8400, "bbox": 33600})


if __name__ == "__main__":
    unittest.main()
