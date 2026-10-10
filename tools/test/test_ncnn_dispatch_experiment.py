"""A private dispatch experiment must never silently patch a different NCNN."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'benchmark'))
import ncnn_dispatch_experiment as experiment


class DispatchPatchTests(unittest.TestCase):
    def test_hash_mismatch_rejected(self):
        with self.assertRaisesRegex(ValueError, 'hash'):
            experiment.patch_net(b'not the pinned source')

    def test_only_bounded_threshold_changes_and_waits_survive(self):
        source = Path(os.environ.get('HV_NCNN_BASELINE_NET',
                      experiment.ROOT / 'out/ncnn-20260526/source/src/net.cpp'))
        if not source.is_file():
            self.skipTest('verified cached pinned source required')
        original = source.read_bytes()
        changed = experiment.patch_net(original)
        self.assertEqual(changed.replace(experiment.CANDIDATE, experiment.BASELINE), original)
        self.assertEqual(changed.count(b'cmd.submit_and_wait()'), original.count(b'cmd.submit_and_wait()'))
        self.assertIn(b'const uint32_t rough_score = vkdev->info.rough_score();', changed)
        self.assertIn(b'if (cmd.pending_dispatch_total() > pending_dispatch_threshold)', changed)
        with self.assertRaisesRegex(ValueError, 'hash'):
            experiment.patch_net(changed)
        scoped = experiment.patch_net(original, adreno_only=True)
        self.assertEqual(scoped.replace(experiment.POLICY_INCLUDE, b'').replace(
                         experiment.SCOPED_CANDIDATE, experiment.BASELINE), original)
        self.assertEqual(scoped.count(b'cmd.submit_and_wait()'), original.count(b'cmd.submit_and_wait()'))

    def test_output_must_be_new_directory_below_out(self):
        with self.assertRaises(ValueError):
            experiment.validate_destination(experiment.ROOT / 'runtime')
        with self.assertRaises(ValueError):
            experiment.validate_destination(experiment.ROOT / 'out')
        with self.assertRaises(ValueError):
            experiment.validate_destination(experiment.ROOT / 'out/../runtime')

    def test_bad_archive_rejected_before_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / 'wrong.zip'
            archive.write_bytes(b'not the pinned archive')
            target = experiment.ROOT / 'out' / ('dispatch-test-' + Path(temp).name)
            with self.assertRaisesRegex(ValueError, 'archive size/hash'):
                experiment.prepare(Path(temp) / 'missing-cache', archive, target)
            self.assertFalse(target.exists())

    def test_sealing_rejects_changed_source_manifest(self):
        import json
        with tempfile.TemporaryDirectory(dir=experiment.ROOT / 'out') as temp:
            directory = Path(temp)
            (directory / 'experiment-source.json').write_text(json.dumps({
                'kind': 'hv-ncnn-private-dispatch-experiment-v1',
                'shipping_eligible': False, 'source_manifest_sha256': '0' * 64}))
            (directory / 'source-manifest.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'manifest'):
                experiment.seal_install(directory)
            self.assertFalse((directory / 'build-receipt.json').exists())


if __name__ == '__main__':
    unittest.main()
