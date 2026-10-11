import unittest
from analyze_combined_benchmark import summarize


def marker(second, text):
    return {'utc': f'2026-10-10T00:00:{second:02d}Z', 'category': 'benchmark.phase', 'message': text}


def person(second, fresh):
    return {'utc': f'2026-10-10T00:00:{second:02d}Z', 'phase': 'Video/Neural/baseline/60s',
            'sourceId': '1', 'generation': '1', 'person': {'stableTrackId': 9, 'seconds': 1,
            'freshObservations': fresh, 'completeCoco17': fresh, 'freshFps': fresh, 'completeCoco17Fps': fresh,
            'localResultAgeMeanMs': 50, 'localResultAgeMaximumMs': 70}}


class CombinedAnalysisTests(unittest.TestCase):
    def test_repeated_baseline_excludes_warmup_and_other_occurrences(self):
        name = 'Video/Neural/baseline/60s'
        events = [marker(10, name+' MEASURE BEGIN; baseline'), marker(13, name+' MEASURE END seconds=3; baseline'),
                  marker(20, name+' MEASURE BEGIN; baseline'), marker(23, name+' MEASURE END seconds=3; baseline')]
        rows = [person(9, 99), person(10, 99), person(11, 20), person(12, 0), person(21, 25), person(24, 99)]
        result = summarize(events, rows)['measurements']
        self.assertEqual(result[0]['people'][0]['freshFps'], 10)
        self.assertEqual(result[1]['people'][0]['freshFps'], 25)

    def test_interrupted_measurement_is_not_reported_as_complete(self):
        result = summarize([marker(10, 'Video/Neural/baseline/60s MEASURE BEGIN; baseline'),
                            marker(12, 'interactive cancelled')], [person(11, 20)])
        self.assertFalse(result['measurements'][0]['complete'])
        self.assertEqual(result['measurements'][0]['people'], [])


if __name__ == '__main__':
    unittest.main()
