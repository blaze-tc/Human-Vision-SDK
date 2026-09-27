from pathlib import Path
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class R4BuilderCapacityTests(unittest.TestCase):
    def setUp(self):
        self.fixture_dir = tempfile.TemporaryDirectory(prefix='r4-capacity-')
        self.addCleanup(self.fixture_dir.cleanup)
        self.manifest = Path(self.fixture_dir.name)/'manifest.json'
        self.manifest.write_text(json.dumps({'annotations': [{'id': str(i)} for i in range(7)]}))

    def run_builder(self, *args):
        source=(ROOT/'tools/test/build_android_topdown_eval.ps1').read_text(encoding='utf-8-sig')
        self.assertIn('[switch]$ResolveConfigurationOnly',source)
        return subprocess.run(['pwsh', '-NoProfile', '-File',
            str(ROOT/'tools/test/build_android_topdown_eval.ps1'), '-ResolveConfigurationOnly', *args],
            cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)

    def test_live_capacity_above_two_rejected(self):
        self.assertNotEqual(self.run_builder('-Capacity', '7').returncode, 0)

    def test_fixture_capacity_and_explicit_override(self):
        for requested, expected in [(None, 7), ('8', 8)]:
            with self.subTest(requested=requested):
                args=['-R4ParityManifest', str(self.manifest)]
                if requested:
                    args += ['-Capacity', requested]
                run=self.run_builder(*args)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertEqual(json.loads(run.stdout)['effective_capacity'], expected)

    def test_capacity_below_annotation_count_rejected(self):
        self.assertNotEqual(self.run_builder('-R4ParityManifest',
            str(self.manifest), '-Capacity', '2').returncode, 0)


if __name__ == '__main__':
    unittest.main()
