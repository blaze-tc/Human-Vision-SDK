"""Candidate conversion safeguards; these tests never pretend to run an NPU."""
import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RknnConversionTests(unittest.TestCase):
    def tool(self):
        path = ROOT / 'tools/models/rknn/convert_candidate.py'
        self.assertTrue(path.exists(), 'Missing reproducible RKNN candidate conversion tool')
        spec = importlib.util.spec_from_file_location('rknn_candidate', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_wrong_model_hash_fails_before_vendor_module_is_loaded(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            model = Path(folder) / 'source.onnx'
            model.write_bytes(b'untrusted model artifact')
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                tool.validate_source(model, '0' * 64)

    def test_int8_needs_real_existing_calibration_images(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            data = Path(folder) / 'calibration.txt'
            data.write_text('absent-person-frame.png\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'calibration'):
                tool.validate_calibration('int8', None)
            with self.assertRaisesRegex(ValueError, 'missing'):
                tool.validate_calibration('int8', data)
            self.assertIsNone(tool.validate_calibration('non-quantized', None))

    def test_candidate_receipt_never_promotes_conversion_to_acceptance(self):
        tool = self.tool()
        receipt = tool.candidate_receipt('a' * 64, 'b' * 64, 'int8', '2.3.2')
        self.assertFalse(receipt['deployment_ready'])
        self.assertFalse(receipt['numerical_gate_passed'])
        self.assertFalse(receipt['device_performance_verified'])
        self.assertEqual(receipt['target'], 'rk3588')


if __name__ == '__main__':
    unittest.main()
