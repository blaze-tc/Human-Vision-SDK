"""Candidate conversion safeguards; these tests never pretend to run an NPU."""
import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace

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

    def test_hybrid_requires_real_calibration(self):
        with self.assertRaisesRegex(ValueError, 'calibration'):
            self.tool().validate_calibration('hybrid', None)

    def test_hybrid_ranges_must_exist_and_follow_graph_edges(self):
        # Pure graph contract test; this does not simulate vendor inference.
        node = lambda ins, outs: SimpleNamespace(input=ins, output=outs)
        graph = SimpleNamespace(node=[node(['image'], ['head']), node(['head'], ['points']),
                                      node(['image'], ['boxes'])])
        tool = self.tool()
        self.assertEqual(tool.validate_hybrid_ranges(graph, [['head', 'points']]), [['head', 'points']])
        for ranges in [[], [['missing', 'points']], [['points', 'head']],
                       [['head', 'boxes']], [['head', 'points'], ['head', 'points']],
                       [['head']], [['image', 'points']]]:
            with self.assertRaises(ValueError, msg=str(ranges)):
                tool.validate_hybrid_ranges(graph, ranges)

    def test_hybrid_config_binds_source_and_preserves_candidate_status(self):
        import json
        tool = self.tool()
        graph = SimpleNamespace(node=[SimpleNamespace(input=['image'], output=['head']),
                                      SimpleNamespace(input=['head'], output=['points'])])
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'hybrid.json'
            value = dict(schema_version=1, source_onnx_sha256='a'*64, ranges=[['head', 'points']])
            path.write_text(json.dumps(value), encoding='utf-8')
            self.assertEqual(tool.read_hybrid_config(path, 'a'*64, graph)[0], value['ranges'])
            with self.assertRaisesRegex(ValueError, 'source'):
                tool.read_hybrid_config(path, 'b'*64, graph)
            value['ignore_accuracy_failure'] = True
            path.write_text(json.dumps(value), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'keys'):
                tool.read_hybrid_config(path, 'a'*64, graph)


if __name__ == '__main__':
    unittest.main()
