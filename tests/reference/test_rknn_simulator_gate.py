"""Simulator gate boundaries; these do not count as device/inference acceptance."""
import importlib.util
from pathlib import Path
import tempfile
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

    def test_rectangular_gates_check_all_rows_and_reject_low_output(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            for width, height, rows, top in ((640, 384, 5040, 12), (960, 576, 11340, 18)):
                with self.subTest(width=width):
                    tensor = np.zeros((3, height, width), np.float32)
                    raw = [np.full((rows, columns), -10, np.float32) for columns in (65, 51)]
                    geometry = tool.fixed_landscape_geometry(width, height, 1024, 576)
                    self.assertEqual(geometry['top'], top)
                    metadata = dict(geometry=geometry, expected_people=0, input_sha256='fixture')
                    bank = [('empty', metadata, tensor, raw)]
                    self.assertTrue(tool.evaluate(bank, lambda _: raw, output / str(width), 'unit', {})['offline_numerical_passed'])
                    with self.assertRaisesRegex(ValueError, 'shape mismatch'):
                        tool.evaluate(bank, lambda _: [value[:3024] for value in raw], output / f'wrong-{width}', 'unit', {})


if __name__ == '__main__':
    unittest.main()
