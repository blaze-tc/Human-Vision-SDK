import importlib.util
from pathlib import Path
import unittest

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location('trace', HERE / 'ncnn_execution_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def records():
    return [dict(event='begin', frame=64),
            dict(event='submit', frame=64, segment=1, command=123, wall_us=100, pairs=1, phase=1),
            dict(event='query', frame=64, segment=1, command=123, layer=2,
                 start=100, end=130, available_start=1, available_end=1,
                 status=0, bits=64, period_ns=1),
            dict(event='end', frame=64, segments=1, success=1)]


class TraceTests(unittest.TestCase):
    def test_valid_reports_gpu_and_wall_separately(self):
        result = trace.aggregate(records())
        self.assertAlmostEqual(result['frames'][0]['gpu_layer_us'], .03)
        self.assertEqual(result['frames'][0]['submit_wall_us'], 100)
        self.assertNotIn('cpu_overhead_us', result['frames'][0])

    def test_missing_query_rejected(self):
        value = records(); del value[2]
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_unavailable_rejected(self):
        value = records(); value[2]['available_end'] = 0
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_read_failure_rejected(self):
        value = records(); value[2]['status'] = 1
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_invalid_bits_period_rejected(self):
        for key, val in [('bits', 0), ('bits', 65), ('period_ns', 0), ('period_ns', float('nan'))]:
            value = records(); value[2][key] = val
            with self.assertRaises(ValueError): trace.aggregate(value)

    def test_backward_timestamp_rejected(self):
        value = records(); value[2]['end'] = 99
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_duplicate_query_rejected(self):
        value = records(); value.insert(3, value[2].copy())
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_overlapping_layers_rejected(self):
        value = records(); value[1]['pairs'] = 2
        other = value[2].copy(); other['layer'] = 3; other['start'] = 120; other['end'] = 150
        value.insert(3, other)
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_truncated_frame_rejected(self):
        with self.assertRaises(ValueError): trace.aggregate(records()[:-1])

    def test_wrong_command_rejected(self):
        value = records(); value[2]['command'] = 124
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_wrapped_timestamp_is_rejected_conservatively(self):
        value = records(); value[2].update(bits=8, start=250, end=10)
        with self.assertRaises(ValueError): trace.aggregate(value)

    def test_patch_drift_rejected(self):
        with self.assertRaises(ValueError): trace.patch_source('src/net.cpp', b'drift')

    def test_submission_phase_required(self):
        value = records(); del value[1]['phase']
        with self.assertRaises(ValueError): trace.aggregate(value)

    @unittest.skipUnless((trace.ROOT / 'out/ncnn-20260526/source/src/net.cpp').is_file(), 'Pinned cached source required for positive patch check')
    def test_patched_source_preserves_wait_decisions(self):
        source = trace.ROOT / 'out/ncnn-20260526/source'
        for name in trace.BEFORE:
            data = (source/name).read_bytes()
            patched = trace.patch_source(name, data)
            for token in [b'pending_dispatch_threshold', b'cmd_submit_and_wait = true;', b'cmd.reset();']:
                self.assertEqual(data.count(token), patched.count(token))
            self.assertEqual(data.count(b'vkWaitForFences('), patched.count(b'vkWaitForFences('))
            with self.assertRaises(ValueError): trace.patch_source(name, data + b' ')


if __name__ == '__main__': unittest.main()
