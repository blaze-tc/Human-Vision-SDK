"""The diagnostic comparator must never turn finite execution into a parity pass."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('rtmo_compare', ROOT / 'tools/models/ncnn/rtmo_compare.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)


class RtmoComparisonTests(unittest.TestCase):
    def fixture(self, root, nonfinite):
        for folder in ('one', 'android-raw-corrected/one', 'android-dcc-shape-corrected/one'):
            (root / folder).mkdir(parents=True)
        for name in ('cls16','bbox16','vis16','pose16','cls32','bbox32','vis32','pose32','keypoints'):
            count = 408 if name == 'keypoints' else 4
            np.ones(count, np.float32).tofile(root / 'one' / f'ort-{name}.fp32')
            # Deliberately finite-but-wrong: must not be called a numerical pass.
            value = np.zeros(count, np.float32)
            if nonfinite and name == 'pose16':
                value[0] = np.nan
            folder = 'android-dcc-shape-corrected/one' if name == 'keypoints' else 'android-raw-corrected/one'
            value.tofile(root / folder / f'ncnn-gpu-{name}.fp32')

    def test_nonfinite_is_failure_and_json_contains_no_nan(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            self.fixture(root, True)
            report = comparison.compare(root)
            self.assertEqual(report['status'], 'FAIL_NONFINITE_OR_ELEMENT_COUNT')
            self.assertTrue(report['results']['raw_pose16']['element_count_matches'])
            self.assertNotIn('shape_matches', report['results']['raw_pose16'])
            self.assertEqual(report['results']['raw_pose16']['nonfinite_count'], 1)
            self.assertIsNone(report['results']['raw_pose16']['max_abs'])
            self.assertNotIn('NaN', json.dumps(report, allow_nan=False))

    def test_finite_but_wrong_is_not_validated(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            self.fixture(root, False)
            report = comparison.compare(root)
            self.assertEqual(report['status'], 'NOT_VALIDATED')
            self.assertEqual(report['results']['dcc_keypoints']['valid_slot0_xy_max'], 1.0)


if __name__ == '__main__':
    unittest.main()
