import tempfile
import unittest
from pathlib import Path

from tools.models.ncnn.prepare_rtmpose_eval import verify_static_body26, prepare
from tools.models.ncnn.finalize_rtmpose_vulkan_shapes import finalize

ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'out/c3-local-runtime/first-norm-reducel2'


class PoseConversionTest(unittest.TestCase):
    def test_fixed_graph_outputs_and_shape_only_finalizer(self):
        if not (EVIDENCE/'model.onnx').is_file():self.skipTest('local model cache not generated')
        result=verify_static_body26(EVIDENCE/'model.onnx')
        self.assertEqual(result['simcc_x']['shape'],[1,26,384])
        self.assertEqual(result['simcc_y']['shape'],[1,26,512])
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'vulkan.param'
            self.assertEqual(finalize(EVIDENCE/'model.param',target)['rewritten_layers'],12)
            self.assertEqual(target.read_bytes(),(EVIDENCE/'vulkan.param').read_bytes())
            bad=Path(directory)/'bad.param';bad.write_bytes((EVIDENCE/'model.param').read_bytes()+b'bad')
            with self.assertRaisesRegex(ValueError,'SHA-256'):finalize(bad,target)


if __name__=='__main__':unittest.main()
