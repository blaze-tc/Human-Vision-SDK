"""Offline replacement candidates must fail closed; never promote a model here."""
import tempfile
import unittest
from pathlib import Path

import numpy as np

from tools.models.rknn.rtmpose_candidate import (
    decode, compare_heads, fixed_model, BODY_SHA256, verify_hash, validate_features, chain_pool5,
)


class CandidateTests(unittest.TestCase):
    def heads(self):
        x = np.zeros((1, 26, 384), np.float32)
        y = np.zeros((1, 26, 512), np.float32)
        x[:, :, 100] = 0.8
        y[:, :, 160] = 0.7
        return [x, y]

    def test_same_heads_and_affine(self):
        inverse = np.array([[2, 0, 10], [0, 2, 20]], np.float32)
        result = decode(self.heads(), inverse, [0, 0, 300, 400])
        self.assertEqual(result['points'][0], [110, 180])
        self.assertEqual(compare_heads(self.heads(), self.heads(), inverse,
                         [0, 0, 300, 400], (300, 400))['valid_count'], 26)

    def test_reject_shape_and_nonfinite(self):
        for bad in ([np.zeros((1, 17, 384)), self.heads()[1]],
                    [np.full((1, 26, 384), np.nan), self.heads()[1]]):
            with self.assertRaises(ValueError):
                decode(bad, np.eye(2, 3), [0, 0, 300, 400])

    def test_reject_large_joint_shift(self):
        bad = self.heads()
        bad[0][:] = 0
        bad[0][:, :, 220] = 0.8
        with self.assertRaises(ValueError):
            compare_heads(self.heads(), bad, np.eye(2, 3),
                          [0, 0, 192, 256], (192, 256))

    def test_reject_confidence_change_and_empty_coverage(self):
        bad = [v * 0.5 for v in self.heads()]
        with self.assertRaises(ValueError):
            compare_heads(self.heads(), bad, np.eye(2, 3), [0, 0, 192, 256], (192, 256))
        with self.assertRaises(ValueError):
            compare_heads([v * 0 for v in bad], [v * 0 for v in bad],
                          np.eye(2, 3), [0, 0, 192, 256], (192, 256))

    def test_unpinned_source_rejected_before_onnx_load(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'body.onnx'
            path.write_bytes(b'not the source')
            with self.assertRaises(ValueError):
                verify_hash(path, BODY_SHA256)
            with self.assertRaises(ValueError):
                fixed_model(path, Path(directory) / 'fixed.onnx')

    def test_split_boundary_rejects_wrong_layout_or_nan(self):
        valid = np.zeros((1, 26, 8, 6), np.float32)
        self.assertIs(validate_features([valid]), valid)
        for bad in ([valid.reshape(1, 8, 6, 26)], [valid * np.nan], [], [valid, valid]):
            with self.assertRaises(ValueError):
                validate_features(bad)

    def pool_model(self):
        import onnx
        from onnx import helper as h, TensorProto as T
        nodes = [h.make_node('MaxPool', ['340'], [str(341+i)], name=f'MaxPool_{96+i}',
                            kernel_shape=[k, k], pads=[k//2]*4, strides=[1, 1], ceil_mode=0)
                 for i, k in enumerate([5, 9, 13])]
        model = h.make_model(h.make_graph(nodes, 'pool', [h.make_tensor_value_info('340', T.FLOAT, [1, 2, 8, 6])],
                              [h.make_tensor_value_info(str(n), T.FLOAT, [1, 2, 8, 6]) for n in [341, 342, 343]]),
                            opset_imports=[h.make_opsetid('', 13)])
        model.ir_version = 8
        return model

    def test_pool_rewrite_exact_including_negative_border(self):
        import onnxruntime as ort
        original = self.pool_model()
        changed = chain_pool5(self.pool_model())
        options = ort.SessionOptions()
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
        sessions = [ort.InferenceSession(m.SerializeToString(), options) for m in [original, changed]]
        rng = np.random.default_rng(7)
        for value in [rng.normal(size=(1, 2, 8, 6)).astype(np.float32),
                      -np.arange(1, 97, dtype=np.float32).reshape(1, 2, 8, 6)]:
            a, b = [s.run(None, {'340': value}) for s in sessions]
            for x, y in zip(a, b):
                np.testing.assert_array_equal(x, y)

    def test_pool_rewrite_refuses_changed_contract(self):
        model = self.pool_model()
        model.graph.node[1].input[0] = 'other'
        with self.assertRaises(ValueError):
            chain_pool5(model)


if __name__ == '__main__':
    unittest.main()
