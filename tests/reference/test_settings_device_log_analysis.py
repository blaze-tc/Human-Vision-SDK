import importlib.util
from pathlib import Path
import unittest
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('settings_device_analysis', ROOT / 'tools/benchmark/analyze_settings_device_log.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class SettingsDeviceLogAnalysisTests(unittest.TestCase):
    def test_force_stop_tail_is_reported_without_hiding_interior_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            path = folder / 'hardware-0.jsonl'
            path.write_text('{"gpuPercent":72}\n{"gpuStatus":"partial', encoding='utf-8')
            warnings = []
            records = analysis.json_lines(folder, 'hardware', warnings)
            self.assertEqual(records, [{'gpuPercent': 72}])
            self.assertEqual(len(warnings), 1)
            self.assertIn('hardware-0.jsonl', warnings[0])
            path.write_text('{invalid}\n{"gpuPercent":72}\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                analysis.json_lines(folder, 'hardware', [])

    def test_quality_switch_splits_measurements_without_restarting_input(self):
        self.assertTrue(hasattr(analysis, 'group_active_rows'), 'analysis must split live quality switches')
        rows, timings = [], []
        for i, profile in enumerate(['medium', 'medium', 'low', 'low']):
            stamp = f'2026-10-09T03:00:0{i}.0000000Z'
            rows.append(dict(utc=stamp, elapsed_s=str(i), sdk_state='Running', source_state='Streaming',
                             source_id='6', generation='2', source_mode='Rtsp', processed=str(10*i)))
            timings.append(dict(utc=stamp, sourceId='6', generation='2', runtimeProfile=profile, modelPack=profile+'-pack'))
        groups = analysis.group_active_rows(rows, timings)
        self.assertEqual([len(g) for g in groups], [2, 2])
        self.assertEqual([g[0]['runtime_profile'] for g in groups], ['medium', 'low'])

    def test_local_input_intervals_never_use_packet_pts_as_clock(self):
        self.assertTrue(hasattr(analysis, 'parse_input_timing'), 'missing local input timing parser')
        sample = analysis.parse_input_timing('HVInputGate: gpu_color_completed sequence=64 generation=2 pts_us=-9223372036854775808 received_us=100000 decoded_us=105000 submitted_us=115000 converted_us=118000')
        self.assertEqual(sample['decodeToSubmitMs'], 10)
        self.assertEqual(sample['conversionFencePollMs'], 3)
        self.assertEqual(sample['arrivalToImageObservationMs'], 5)

    def test_missing_or_reversed_input_clocks_remain_unavailable(self):
        self.assertTrue(hasattr(analysis, 'parse_input_timing'), 'missing local input timing parser')
        sample = analysis.parse_input_timing('HVInputGate: gpu_color_completed sequence=64 generation=2 received_us=100000 decoded_us=90000')
        self.assertEqual(sample['arrivalToImageObservationMs'], -1)
        self.assertEqual(sample['conversionFencePollMs'], -1)
        self.assertEqual(sample['decodeToSubmitMs'], -1)


    def test_rknn_breakdown_requires_the_actual_backend_and_valid_times(self):
        text = "Profile=android-rknn-npu-quality-low\nActual backend=backend.rknn\nBackend sessions=body.rknn: backend.rknn -> backend.rknn / 44 ms; core_mask=7; input_set_ms=11.25; execute_ms=29.5; output_get_ms=3.75; output_release_ms=0.002"
        self.assertTrue(hasattr(analysis, 'parse_rknn_timing'))
        sample = analysis.parse_rknn_timing(text)
        self.assertEqual(sample['coreMask'], 7)
        self.assertEqual(sample['executeMs'], 29.5)
        self.assertEqual(sample['backendSumMs'], 44.502)
        self.assertIsNone(analysis.parse_rknn_timing(text.replace('Actual backend=backend.rknn', 'Actual backend=backend.ort.cpu')))
        self.assertEqual(analysis.parse_rknn_timing(text.replace('execute_ms=29.5', 'execute_ms=NaN'))['executeMs'], -1)
        self.assertEqual(analysis.parse_rknn_timing(text.replace('execute_ms=29.5', 'execute_ms=-2'))['executeMs'], -1)
        self.assertEqual(analysis.parse_rknn_timing(text.replace('execute_ms=29.5;', ''))['backendSumMs'], -1)


if __name__ == '__main__':
    unittest.main()
