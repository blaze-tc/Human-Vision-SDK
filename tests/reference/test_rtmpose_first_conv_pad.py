import hashlib
import tempfile
import unittest
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "out/c3-local-runtime/official-preset/model.onnx"


class FirstConvPadTest(unittest.TestCase):
    def test_exact_official_graph_becomes_four_channel_with_zero_fourth_kernel(self):
        from tools.models.ncnn.pad_rtmpose_first_conv import pad_first_conv
        if not SOURCE.is_file():
            self.skipTest('Pinned local evaluation source not downloaded')

        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "padded.onnx"
            result = pad_first_conv(SOURCE, target)
            model = onnx.load(target)
            self.assertEqual(result["source_sha256"], hashlib.sha256(SOURCE.read_bytes()).hexdigest())
            self.assertEqual([d.dim_value for d in model.graph.input[0].type.tensor_type.shape.dim], [1, 4, 256, 192])
            conv = next(node for node in model.graph.node if node.name == "/backbone/stem/stem.0/conv/Conv")
            weight = next(t for t in model.graph.initializer if t.name == conv.input[1])
            original = onnx.load(SOURCE)
            original_weight = next(t for t in original.graph.initializer if t.name == conv.input[1])
            actual = numpy_helper.to_array(weight)
            np.testing.assert_array_equal(actual[:, :3], numpy_helper.to_array(original_weight))
            np.testing.assert_array_equal(actual[:, 3], np.zeros((12, 3, 3), dtype=np.float32))
            self.assertEqual(result["target_sha256"], hashlib.sha256(target.read_bytes()).hexdigest())
            changed = Path(temporary) / "changed.onnx"
            changed.write_bytes(SOURCE.read_bytes() + b"x")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                pad_first_conv(changed, target)
            self.assertEqual(result["target_sha256"], hashlib.sha256(target.read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
