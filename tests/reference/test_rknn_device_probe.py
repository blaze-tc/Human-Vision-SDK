"""Actual executable failure paths; requires a Linux build of the device probe."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RknnDeviceProbeTests(unittest.TestCase):
    def run_probe(self, *args):
        source = ROOT / 'tools/models/rknn/device_probe.cpp'
        self.assertTrue(source.exists(), 'Missing real RKNN device probe source')
        binary = Path(os.environ['HV_RKNN_PROBE_HOST_BINARY'])
        self.assertTrue(binary.is_file(), 'Compile the actual host executable before running this suite')
        return subprocess.run([str(binary), *map(str, args)], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, timeout=15)

    def test_missing_arguments_fail_with_usage(self):
        result = self.run_probe()
        self.assertEqual(result.returncode, 2)
        self.assertIn('usage:', result.stdout)

    def test_invalid_shape_or_core_selection_fails_before_loading_runtime(self):
        for width, mask in [('invalid', '1'), ('512', '3')]:
            result = self.run_probe('/missing.so', '/missing.rknn', '/missing.rgb',
                                    '/tmp/unused', width, 288, 1, 5, mask)
            self.assertEqual(result.returncode, 2)
            self.assertIn('argument', result.stdout)

    def test_wrong_input_byte_count_fails_without_inference(self):
        with tempfile.TemporaryDirectory() as folder:
            rgb = Path(folder) / 'input.rgb'
            rgb.write_bytes(b'incomplete image')
            model = Path(folder) / 'model.rknn'
            model.write_bytes(b'invalid model; runtime never reached')
            result = self.run_probe('/missing.so', model, rgb, Path(folder) / 'result', 512, 288, 1, 5, 1)
            self.assertEqual(result.returncode, 3)
            self.assertIn('input byte count', result.stdout)

    def test_missing_library_cannot_report_a_successful_npu(self):
        with tempfile.TemporaryDirectory() as folder:
            rgb, model = Path(folder) / 'input.rgb', Path(folder) / 'model.rknn'
            rgb.write_bytes(bytes(512 * 288 * 3))
            model.write_bytes(b'runtime load failure test; no execution')
            result = self.run_probe('/missing.so', model, rgb, Path(folder) / 'result', 512, 288, 1, 5, 1)
            self.assertEqual(result.returncode, 4)
            self.assertIn('runtime load failed', result.stdout)
            self.assertNotIn('"completed":', result.stdout)
            self.assertFalse((Path(folder) / 'result-out0.fp32').exists())


if __name__ == '__main__':
    unittest.main()
