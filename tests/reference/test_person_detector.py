import unittest
import onnx
from tools.reference.prepare_person_detector import person_only_nms


class PersonDetectorTests(unittest.TestCase):
    def test_rejects_graph_without_expected_nms(self):
        model = onnx.helper.make_model(onnx.helper.make_graph([], "empty", [], []))
        with self.assertRaises(ValueError):
            person_only_nms(model)

    def test_preserves_graph_and_gathers_only_person_scores_for_nms(self):
        model = onnx.load("models/detector/rtmdet_tiny_640.onnx")
        original = model.SerializeToString()
        result = person_only_nms(model)
        self.assertEqual(model.SerializeToString(), original)
        nms = next(n for n in result.graph.node if n.op_type == "NonMaxSuppression")
        gather = next(n for n in result.graph.node if n.output == [nms.input[1]])
        self.assertEqual(gather.op_type, "Gather")
        self.assertEqual(next(a.i for a in gather.attribute if a.name == "axis"), 1)
        indices = next(i for i in result.graph.initializer if i.name == gather.input[1])
        self.assertEqual(onnx.numpy_helper.to_array(indices).tolist(), [0])
        onnx.checker.check_model(result)


if __name__ == "__main__":
    unittest.main()
