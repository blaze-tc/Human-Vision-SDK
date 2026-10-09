"""Bounded source recovery guards; real graph parity is a separate integration gate."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NcnnRecoveryTests(unittest.TestCase):
    def tool(self):
        path = ROOT / 'tools/models/rknn/recover_pinned_onnx.py'
        self.assertTrue(path.exists(), 'Missing pinned NCNN-to-ONNX recovery tool')
        spec = importlib.util.spec_from_file_location('pinned_recovery', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_unpinned_weights_fail_before_any_graph_export(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            param, weights = Path(folder) / 'model.param', Path(folder) / 'model.bin'
            param.write_text('7767517\n1 1\nInput in0 0 1 in0\n')
            weights.write_bytes(b'unknown trained weights')
            with self.assertRaisesRegex(ValueError, 'pinned.*SHA'):
                tool.validate_source(param, weights)

    def test_half_weights_align_to_four_bytes_then_bias_is_float32(self):
        tool = self.tool()
        # Three real FP16 values plus one alignment half, then two FP32 biases.
        blob = struct.pack('<I4e2f', 0x01306B47, 1.0, -2.0, .5, 0.0, .25, -.75)
        reader = tool.WeightReader(blob)
        self.assertEqual(reader.weights(3).tolist(), [1.0, -2.0, .5])
        self.assertEqual(reader.bias(2).tolist(), [.25, -.75])
        reader.finish()
        with self.assertRaisesRegex(ValueError, 'truncated'):
            tool.WeightReader(blob[:8]).weights(3)

    def test_unknown_weight_encoding_and_trailing_bytes_are_rejected(self):
        tool = self.tool()
        with self.assertRaisesRegex(ValueError, 'weight encoding'):
            tool.WeightReader(struct.pack('<I', 7)).weights(1)
        with self.assertRaisesRegex(ValueError, 'trailing'):
            tool.WeightReader(b'leftover').finish()

    def test_unknown_graph_operator_and_bad_layer_count_are_rejected(self):
        tool = self.tool()
        with self.assertRaisesRegex(ValueError, 'unsupported operator'):
            tool.parse_layers('7767517\n1 1\nUntrusted x 0 1 y\n')
        with self.assertRaisesRegex(ValueError, 'layer count'):
            tool.parse_layers('7767517\n2 1\nInput in0 0 1 in0\n')


if __name__ == '__main__':
    unittest.main()
