import tempfile
import unittest
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import helper, TensorProto

from tools.models.ncnn.fuse_rtmpose_first_norm import fuse


class FirstNormFusionTests(unittest.TestCase):
    def test_pinned_fusion_preserves_cpu_norm_and_other_nodes(self):
        source = Path('out/c3-local-runtime/padded-first-conv/model.onnx')
        if not source.is_file():
            self.skipTest('Pinned local evaluation model not downloaded')
        original = onnx.load(source)
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory) / 'fused.onnx'
            fuse(source, dest)
            fused = onnx.load(dest)
            removed = set(range(130, 136))
            self.assertEqual([n.SerializeToString() for i,n in enumerate(original.graph.node) if i not in removed],
                             [n.SerializeToString() for n in fused.graph.node if n.name != '/mlp/mlp.0/ReduceL2'])
            nodes = [n for n in fused.graph.node if n.name == '/mlp/mlp.0/ReduceL2']
            self.assertEqual(len(nodes), 1)
            self.assertEqual(list(nodes[0].output), ['/mlp/mlp.0/Pow_1_output_0'])
            def session(nodes):
                graph=helper.make_graph(nodes,'first_norm',[helper.make_tensor_value_info('/Reshape_output_0',TensorProto.FLOAT,[1,26,48])],[helper.make_tensor_value_info('/mlp/mlp.0/Pow_1_output_0',TensorProto.FLOAT,[1,26,1])])
                model=helper.make_model(graph,opset_imports=[helper.make_opsetid('',11)])
                return ort.InferenceSession(model.SerializeToString(),providers=['CPUExecutionProvider'])
            old=session(list(original.graph.node)[130:136]); new=session(nodes)
            rng=np.random.default_rng(90)
            for x in (np.full((1,26,48),50,np.float32),rng.normal(0,70,(1,26,48)).astype(np.float32),np.zeros((1,26,48),np.float32)):
                np.testing.assert_allclose(old.run(None,{'/Reshape_output_0':x})[0],new.run(None,{'/Reshape_output_0':x})[0],rtol=2e-6,atol=1e-6)
            bad=Path(directory)/'bad.onnx';bad.write_bytes(source.read_bytes()+b'\x00')
            with self.assertRaisesRegex(ValueError,'SHA-256'):
                fuse(bad,Path(directory)/'rejected.onnx')


if __name__ == '__main__':
    unittest.main()
