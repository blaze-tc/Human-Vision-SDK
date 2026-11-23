import importlib.util
from pathlib import Path
import hashlib
import json
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'patch_runtime_prefab_interactive.py'
spec = importlib.util.spec_from_file_location('interactive_patch', SCRIPT)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class InteractivePatchSafetyTests(unittest.TestCase):
    def test_applies_only_the_owned_anchor_and_keeps_private_content(self):
        before = 'private extension\nConnectRtspOnly();\nprivate footer\n'
        after = before.replace('ConnectRtspOnly();', 'ConnectSelectedSource();')
        self.assertEqual(after, patch.transform(before, patch.digest(before), patch.digest(after),
            [('ConnectRtspOnly();', 'ConnectSelectedSource();')]))

    def test_unknown_source_is_rejected(self):
        before, after = 'old();\n', 'new();\n'
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            patch.transform(before + '// user edit\n', patch.digest(before), patch.digest(after), [('old();', 'new();')])

    def test_ambiguous_anchor_is_rejected_even_when_source_hash_matches(self):
        before = 'old(); old();\n'
        with self.assertRaisesRegex(ValueError, 'anchor'):
            patch.transform(before, patch.digest(before), patch.digest('new(); old();\n'), [('old();', 'new();')])

    def test_wrong_payload_hash_is_rejected(self):
        before = 'old();\n'
        with self.assertRaisesRegex(ValueError, 'result'):
            patch.transform(before, patch.digest(before), patch.digest('different();\n'), [('old();', 'new();')])

    def test_replay_is_idempotent(self):
        before, after = 'old();\n', 'new();\n'
        self.assertEqual(after, patch.transform(after, patch.digest(before), patch.digest(after), [('old();', 'new();')]))


class InteractiveStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'Human-Vision-SDK-Test'
        self.backup = self.root / 'backup'
        self.payload = self.root / 'payload'
        self.payload.mkdir()
        self.project.mkdir()
        self.before = 'private prefix\nold();\nprivate suffix\n'
        self.after = self.before.replace('old();', 'new();')
        self.relative = 'Assets/HumanVision/Samples/RuntimePrefab/Owned.cs'
        self.target = self.project / self.relative
        self.target.parent.mkdir(parents=True)
        self.target.write_bytes(self.before.replace('\n', '\r\n').encode())
        (self.payload / 'New.cs').write_bytes(b'new partial source\n')
        self.manifest = {'patches': [{'path': self.relative, 'before': patch.digest(self.before),
            'after': patch.digest(self.after), 'replacements': [('old();', 'new();')]}],
            'payloads': [{'path': 'Assets/HumanVision/Samples/RuntimePrefab/New.cs', 'payload': 'New.cs',
                'sha256': hashlib.sha256((self.payload / 'New.cs').read_bytes()).hexdigest()}]}
        self.write_manifest()

    def write_manifest(self):
        (self.payload / 'patches.json').write_text(json.dumps(self.manifest), encoding='utf-8')

    def test_dry_run_never_changes_sources_or_creates_backups(self):
        before = self.target.read_bytes()
        receipt = patch.stage(self.project, self.backup, payload_root=self.payload)
        self.assertEqual(before, self.target.read_bytes())
        self.assertFalse(self.backup.exists())
        self.assertFalse((self.target.parent / 'New.cs').exists())
        self.assertEqual(2, sum(item['changed'] for item in receipt['files']))

    def test_apply_preserves_original_bytes_and_replay_is_a_noop(self):
        before = self.target.read_bytes()
        patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(self.after, self.target.read_text())
        self.assertEqual(before, (self.backup / self.relative).read_bytes())
        receipt = patch.stage(self.project, self.backup, True, self.payload)
        self.assertFalse(any(item['changed'] for item in receipt['files']))

    def test_unknown_later_source_leaves_earlier_file_untouched(self):
        later = self.target.parent / 'Later.cs'; later.write_text('unreviewed();\n')
        self.manifest['patches'].append({'path': str(later.relative_to(self.project)),
            'before': patch.digest('reviewed();\n'), 'after': patch.digest('changed();\n'),
            'replacements': [('reviewed();', 'changed();')]})
        self.write_manifest(); before = self.target.read_bytes()
        with self.assertRaisesRegex(ValueError, 'Unknown'):
            patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(before, self.target.read_bytes())
        self.assertFalse(self.backup.exists())

    def test_conflicting_backup_is_checked_before_any_write(self):
        saved = self.backup / self.relative; saved.parent.mkdir(parents=True); saved.write_text('other backup')
        before = self.target.read_bytes()
        with self.assertRaisesRegex(ValueError, 'different immutable backup'):
            patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(before, self.target.read_bytes())
        self.assertFalse((self.target.parent / 'New.cs').exists())

    def test_changed_payload_is_rejected_before_any_write(self):
        (self.payload / 'New.cs').write_text('partially edited payload')
        before = self.target.read_bytes()
        with self.assertRaisesRegex(ValueError, 'payload hash'):
            patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(before, self.target.read_bytes())

    def test_an_existing_unknown_payload_is_preserved(self):
        unknown = self.target.parent / 'New.cs'; unknown.write_text('user code')
        before = self.target.read_bytes()
        with self.assertRaisesRegex(ValueError, 'Unknown existing'):
            patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(before, self.target.read_bytes()); self.assertEqual('user code', unknown.read_text())

    def test_a_recorded_previous_candidate_can_be_upgraded(self):
        previous = b'reviewed previous candidate\n'
        existing = self.target.parent / 'New.cs'; existing.write_bytes(previous)
        self.manifest['payloads'][0]['previous_sha256'] = [hashlib.sha256(previous).hexdigest()]
        self.write_manifest()
        patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual((self.payload / 'New.cs').read_bytes(), existing.read_bytes())
        self.assertEqual(previous, (self.backup / existing.relative_to(self.project)).read_bytes())

    def test_a_recorded_previous_source_uses_only_its_reviewed_upgrade(self):
        previous = self.after
        final = self.after.replace('new();', 'configured();\nnew();')
        self.target.write_text(previous, encoding='utf-8')
        source = self.manifest['patches'][0]
        source['after'] = patch.digest(final)
        source['replacements'].append(('new();', 'configured();\nnew();'))
        source['upgrades'] = [{'before': patch.digest(previous), 'replacements': [('new();', 'configured();\nnew();')]}]
        self.write_manifest()
        patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(final, self.target.read_text(encoding='utf-8'))
        self.assertEqual(previous, (self.backup / self.relative).read_text(encoding='utf-8'))
        self.assertFalse(any(item['changed'] for item in patch.stage(self.project, self.backup, True, self.payload)['files']))

    def test_a_recorded_upgrade_still_checks_its_final_result_hash(self):
        self.target.write_text(self.after, encoding='utf-8')
        source = self.manifest['patches'][0]
        source['after'] = patch.digest('different final source')
        source['upgrades'] = [{'before': patch.digest(self.after), 'replacements': [('new();', 'wrong();')]}]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'result'):
            patch.stage(self.project, self.backup, True, self.payload)
        self.assertEqual(self.after, self.target.read_text(encoding='utf-8'))
        self.assertFalse(self.backup.exists())

    def test_other_projects_are_never_modified(self):
        with self.assertRaisesRegex(ValueError, 'Only the independent'):
            patch.stage(self.root / 'BusinessGame', self.backup, True, self.payload)

    def test_path_escape_is_rejected(self):
        self.manifest['patches'][0]['path'] = '../outside.cs'
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'Invalid acceptance patch target'):
            patch.stage(self.project, self.backup, True, self.payload)


if __name__ == '__main__':
    unittest.main()
