"""Real ONNX execution tests for a weights-preserving layout rewrite."""
import unittest
import numpy as np
import onnx
from onnx import helper as h, TensorProto as T
import onnxruntime as ort
from tools.models.rknn.optimize_output_graph import optimize


def fixture(shared=False):
    inputs = [h.make_tensor_value_info('x'+str(i), T.FLOAT, [1, 2, i+1]) for i in range(3)]
    nodes = [h.make_node('Transpose', ['x'+str(i)], ['t'+str(i)], perm=[0, 2, 1]) for i in range(3)]
    nodes.append(h.make_node('Concat', ['t0', 't1', 't2'], ['out'], axis=1))
    outputs = [h.make_tensor_value_info('out', T.FLOAT, [1, 6, 2])]
    if shared:
        nodes.append(h.make_node('Identity', ['t0'], ['shared']))
        outputs.append(h.make_tensor_value_info('shared', T.FLOAT, [1, 1, 2]))
    return h.make_model(h.make_graph(nodes, 'fixture', inputs, outputs),
                        opset_imports=[h.make_opsetid('', 13)], ir_version=8)


class OutputGraphTests(unittest.TestCase):
    def test_reorders_real_tensors_exactly_and_preserves_original_graph(self):
        before = fixture(); serialized = before.SerializeToString()
        after, count = optimize(before)
        self.assertEqual(count, 1)
        self.assertEqual(sum(n.op_type == 'Transpose' for n in after.graph.node), 1)
        feed = {'x'+str(i): np.arange(2*(i+1), dtype=np.float32).reshape(1, 2, i+1)-i for i in range(3)}
        a = ort.InferenceSession(serialized, providers=['CPUExecutionProvider']).run(None, feed)
        b = ort.InferenceSession(after.SerializeToString(), providers=['CPUExecutionProvider']).run(None, feed)
        self.assertEqual(a[0].tobytes(), b[0].tobytes())
        self.assertEqual(before.SerializeToString(), serialized)

    def test_shared_or_wrong_permutation_is_not_rewritten(self):
        with self.assertRaisesRegex(ValueError, 'eligible'):
            optimize(fixture(shared=True))
        model = fixture()
        model.graph.node[0].attribute[0].ints[:] = [0, 1, 2]
        with self.assertRaisesRegex(ValueError, 'eligible'):
            optimize(model)

    def test_generated_name_collision_fails_without_mutating_model(self):
        model = fixture()
        model.graph.initializer.append(h.make_tensor('out_before_transpose', T.FLOAT, [1], [0]))
        with self.assertRaisesRegex(ValueError, 'collision'):
            optimize(model)


if __name__ == '__main__':
    unittest.main()
