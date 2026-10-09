"""Simulator gate boundaries; these do not count as device/inference acceptance."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


class RknnSimulatorGateTests(unittest.TestCase):
    def tool(self):
        path = ROOT / 'tools/models/rknn/simulator_gate.py'
        self.assertTrue(path.exists(), 'Missing separately measured RKNN simulator gate')
        spec = importlib.util.spec_from_file_location('simulator_gate', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_output_layout_is_checked_instead_of_arbitrary_reshape(self):
        tool = self.tool()
        self.assertEqual(tool.canonical_output(np.zeros((1, 3, 65), np.float32), 3, 65).shape, (3, 65))
        with self.assertRaisesRegex(ValueError, 'shape'):
            tool.canonical_output(np.zeros((1, 65, 3), np.float32), 3, 65)

    def test_nonfinite_tensor_cannot_enter_pose_comparison(self):
        tool = self.tool()
        bad = np.zeros((1, 3, 65), np.float32)
        bad[0, 1, 0] = np.nan
        with self.assertRaisesRegex(ValueError, 'nonfinite'):
            tool.canonical_output(bad, 3, 65)

    def test_byte_input_reconstruction_requires_exact_uint8_grid(self):
        tool = self.tool()
        byte = np.array([[[1, 128, 255]]], np.uint8)
        chw = byte.transpose(2, 0, 1).astype(np.float32) / np.float32(255)
        self.assertTrue(np.array_equal(tool.uint8_rgb(chw), byte))
        with self.assertRaisesRegex(ValueError, 'uint8 grid'):
            tool.uint8_rgb(np.full((3, 1, 1), .5, np.float32))


if __name__ == '__main__':
    unittest.main()
