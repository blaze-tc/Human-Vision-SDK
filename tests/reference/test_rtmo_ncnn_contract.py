"""Offline RTMO graph contract: never substitutes generated detections for inference."""
import importlib.util
import unittest
from pathlib import Path
import tempfile

import numpy as np
import onnx
import onnxruntime as ort

ROOT = Path(__file__).resolve().parents[2]


class RtmoContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'tools/models/ncnn/rtmo_contract.py'
        spec = importlib.util.spec_from_file_location('rtmo_contract', path)
        cls.contract = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.contract)

    def test_wrong_source_hash_fails_before_export(self):
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            self.contract.verify_source(Path(__file__))

    def test_official_source_and_raw_shapes(self):
        source = ROOT / 'modelpacks/rtmo-t-416/body.onnx'
        self.contract.verify_source(source)
        model = self.contract.extract(onnx.load(source),
                                      {'input': ('input', [1,3,416,416])},
                                      self.contract.RAW_OUTPUTS)
        self.assertFalse({'NonMaxSuppression', 'TopK'} & {n.op_type for n in model.graph.node})
        self.assertEqual(sum(np.prod(v[1]) for v in self.contract.RAW_OUTPUTS.values()), 180830)
        self.assertEqual([d.dim_value for d in model.graph.input[0].type.tensor_type.shape.dim], [1,3,416,416])

    def test_focus_repair_preserves_pixel_and_channel_order(self):
        source = onnx.load(ROOT / 'modelpacks/rtmo-t-416/body.onnx')
        graph = self.contract.extract(source, {'input': ('input', [1,3,416,416])},
                                      {'focus': ('input.1', [1,12,208,208])})
        # Preserve the pinned boundary name required by the repair rule.
        graph.graph.output[0].name = 'input.1'
        next(n for n in graph.graph.node if n.name == 'Concat_24').output[0] = 'input.1'
        repaired = self.contract.repair(graph)
        values = (np.arange(3*416*416, dtype=np.float32) % 251).reshape(1,3,416,416)
        expected = np.concatenate([values[:,:,::2,::2], values[:,:,1::2,::2],
                                   values[:,:,::2,1::2], values[:,:,1::2,1::2]], axis=1)
        for model in (graph, repaired):
            session = ort.InferenceSession(model.SerializeToString(), providers=['CPUExecutionProvider'])
            np.testing.assert_array_equal(session.run(None, {'input': values})[0], expected)

    def test_dcc_boundary_has_static_eight_slots_without_nms(self):
        model = self.contract.extract(onnx.load(ROOT / 'modelpacks/rtmo-t-416/body.onnx'),
                                      self.contract.DCC_INPUTS,
                                      {'keypoints': ('keypoints', [1,8,17,3])})
        self.assertFalse({'NonMaxSuppression', 'TopK', 'Conv'} & {n.op_type for n in model.graph.node})
        self.assertEqual([d.dim_value for d in model.graph.output[0].type.tensor_type.shape.dim], [1,8,17,3])
        self.assertIn('head.dcc.pose_to_kpts.bias', {v.name for v in model.graph.initializer})


if __name__ == '__main__':
    unittest.main()
