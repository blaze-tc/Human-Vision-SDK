"""Red/green archive and immutable source guards; no inference acceptance claims."""
import gzip
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import package_release_snapshot as packaging
from package_live_sdk import metadata

from package_release_snapshot import (file_hashes, sha256, verify_file_closure,
                                      write_tgz, verify_tgz, validate_guids,
                                      verify_unitypackage, write_unitypackage, input_path,
                                      ROOT, PACKAGES)


class SnapshotGuards(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def split_fixture(self):
        for package, files in zip(PACKAGES, (
                {'UPM_INSTALLATION.md': b'Input first, then SDK', 'Runtime/SDK.cs': b'sdk',
                 'Runtime/Plugins/x86_64/sdk.dll': b'sdk-native'},
                {'Runtime/Input.cs': b'input', 'Runtime/Plugins/x86_64/input.dll': b'input-native',
                 'Samples~/Preview/Preview.unity': b'input-scene'})):
            for name, data in files.items():
                path = self.root / 'upm' / package / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                Path(str(path) + '.meta').write_text(metadata(package + '/' + name))
        return self.root

    def test_split_archives_disjoint_guid_partitions_and_exact_full_payload(self):
        root = self.split_fixture()
        expected, translations = packaging.offline_assets(root)
        partitions, actual_translations = packaging.split_offline_assets(root)
        self.assertEqual(actual_translations, translations)
        self.assertEqual(set(partitions), set(PACKAGES))
        sdk, inp = (partitions[name] for name in PACKAGES)
        self.assertTrue(sdk)
        self.assertTrue(inp)
        self.assertFalse(set(sdk) & set(inp))
        self.assertFalse({packaging.guid(meta) for data, meta in sdk.values()} &
                         {packaging.guid(meta) for data, meta in inp.values()})
        self.assertEqual({**inp, **sdk}, expected)
        self.assertIn('Assets/Plugins', inp)
        self.assertIn('Assets/Plugins/x86_64', inp)
        self.assertIn('Assets/HumanVisionInput/Samples/Preview/Preview.unity', inp)
        for name, assets in partitions.items():
            path = self.root / (name + '.unitypackage')
            write_unitypackage(path, assets)
            verify_unitypackage(path, assets)
            with tarfile.open(path, 'r:gz') as archive:
                for member in archive:
                    if member.name.endswith('/asset.meta'):
                        meta = archive.extractfile(member).read()
                        if packaging.is_folder_meta(meta):
                            self.assertNotIn(member.name.removesuffix('.meta'), archive.getnames())

    def test_release_has_exact_eight_public_artifacts_with_split_unitypackages(self):
        root = self.split_fixture()
        source = root / 'tools/package'
        source.mkdir(parents=True)
        for name in ('package_release_snapshot.py', 'package_live_sdk.py', 'check_input_package.py'):
            (source / name).write_bytes((ROOT / 'tools/package' / name).read_bytes())
        (source / 'release-preview4-authority.json').write_text('{}\n')
        with patch.object(packaging, 'validate_snapshot', return_value={}), \
                patch.object(packaging.subprocess, 'check_output', return_value='fixed-head\n'):
            packaging.build(root, root / 'release', {'baseline_external_asset_guids': []})
        expected = {
            'com.blazetc.humanvision-0.4.0-preview.4.tgz',
            'com.blazetc.humanvision.input-0.1.0-preview.2.tgz',
            'HumanVisionSDK-0.4.0-preview.4.unitypackage',
            'HumanVisionInput-0.1.0-preview.2.unitypackage',
            'README.md', 'asset-sha256.json', 'source-snapshot.json', 'SHA256SUMS.txt'}
        self.assertEqual({path.name for path in (root / 'release').iterdir()}, expected)
        sums = (root / 'release/SHA256SUMS.txt').read_text().splitlines()
        self.assertEqual({line.split('  ')[1] for line in sums}, expected - {'SHA256SUMS.txt'})
        for line in sums:
            digest, name = line.split('  ')
            self.assertEqual(digest, sha256((root / 'release' / name).read_bytes()))

    def test_mutable_manifest_cannot_admit_changed_source(self):
        (self.root / 'source.cs').write_bytes(b'reviewed')
        expected = file_hashes(self.root)
        (self.root / 'source.cs').write_bytes(b'changed')
        (self.root / 'asset-sha256.json').write_text(json.dumps(file_hashes(self.root)))
        with self.assertRaisesRegex(ValueError, 'closure'):
            verify_file_closure(self.root, expected)

    def test_missing_and_extra_files_rejected(self):
        (self.root / 'a').write_bytes(b'a')
        expected = file_hashes(self.root)
        (self.root / 'a').unlink()
        with self.assertRaises(ValueError):
            verify_file_closure(self.root, expected)
        (self.root / 'a').write_bytes(b'a')
        (self.root / 'extra').write_bytes(b'b')
        with self.assertRaises(ValueError):
            verify_file_closure(self.root, expected)

    def test_duplicate_guid_rejected(self):
        for name in ('a', 'b'):
            (self.root / (name + '.meta')).write_text('fileFormatVersion: 2\nguid: ' + '1' * 32 + '\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            validate_guids([self.root], set())

    def test_unresolved_prefab_reference_rejected(self):
        (self.root / 'a.prefab').write_text('guid: ' + '2' * 32 + '\n')
        with self.assertRaisesRegex(ValueError, 'Unresolved'):
            validate_guids([self.root], set())

    def test_tgz_reproducible_and_exact(self):
        payload = {'package/a': b'first', 'package/b.meta': b'second'}
        a, b = self.root / 'a.tgz', self.root / 'b.tgz'
        write_tgz(a, payload)
        write_tgz(b, payload)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        verify_tgz(a, payload)
        with self.assertRaisesRegex(ValueError, 'closure'):
            verify_tgz(a, {'package/a': b'tampered'})

    def test_unsafe_tar_and_duplicate_tar_member_rejected(self):
        for names in (['../escape'], ['package/a', 'package/a']):
            path = self.root / 'bad.tgz'
            with tarfile.open(path, 'w:gz') as archive:
                for name in names:
                    member = tarfile.TarInfo(name)
                    member.size = 1
                    archive.addfile(member, io.BytesIO(b'a'))
            with self.assertRaises(ValueError):
                verify_tgz(path, {'package/a': b'a'})

    def test_unitypackage_reproducible_meta_tamper_rejected(self):
        meta = b'fileFormatVersion: 2\nguid: ' + b'1' * 32 + b'\nDefaultImporter:\n'
        assets = {'Assets/Example.txt': (b'example', meta)}
        a, b = self.root / 'a.unitypackage', self.root / 'b.unitypackage'
        write_unitypackage(a, assets)
        write_unitypackage(b, assets)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        verify_unitypackage(a, assets)
        with self.assertRaisesRegex(ValueError, 'closure'):
            verify_unitypackage(a, {'Assets/Example.txt': (b'changed', meta)})

    def test_input_sample_path_visible_and_plugins_have_shared_owner(self):
        self.assertEqual(input_path('Samples~/InputPreview/InputPreview.unity'),
                         'Assets/HumanVisionInput/Samples/InputPreview/InputPreview.unity')
        self.assertEqual(input_path('Runtime/Plugins/x86_64/humanvision_input.dll'),
                         'Assets/Plugins/x86_64/humanvision_input.dll')

    def test_each_git_package_has_local_byte_preservation_attributes(self):
        # Unity's package-only checkout loses the repository-root attributes.
        for name in PACKAGES:
            with self.subTest(package=name):
                attributes = ROOT / 'upm' / name / '.gitattributes'
                self.assertTrue(attributes.is_file(), str(attributes))
                self.assertIn('* -text', attributes.read_text().splitlines())

    def test_package_only_git_checkout_preserves_complete_byte_closure(self):
        for name in PACKAGES:
            with self.subTest(package=name):
                source = ROOT / 'upm' / name
                fixture = self.root / name
                shutil.copytree(source, fixture)
                expected = file_hashes(fixture)

                def git(autocrlf, *args):
                    subprocess.run(['git', '-C', str(fixture), '-c',
                                    'core.autocrlf=' + autocrlf, *args],
                                   check=True, capture_output=True)

                # Build the index from the pinned archive bytes, then reproduce
                # Unity's Windows subtree checkout without parent attributes.
                git('false', 'init')
                git('false', 'add', '.')
                exported = self.root / (name + '-checkout')
                exported.mkdir()
                git('true', 'checkout-index', '--all',
                    '--prefix=' + exported.as_posix() + '/')
                actual = file_hashes(exported)
                changed = sorted(path for path in set(expected) | set(actual)
                                 if expected.get(path) != actual.get(path))
                self.assertEqual(changed, [],
                                 'Git autocrlf changed package bytes: ' + ', '.join(changed))

    def test_folder_exports_metadata_and_pathname_without_regular_asset(self):
        meta = b'fileFormatVersion: 2\nguid: ' + b'1' * 32 + b'\nfolderAsset: yes\nDefaultImporter:\n'
        path = self.root / 'folder.unitypackage'
        write_unitypackage(path, {'Assets/ExampleFolder': (b'', meta)})
        with tarfile.open(path, 'r:gz') as archive:
            files = {member.name for member in archive if member.isfile()}
        self.assertEqual(files, {'1' * 32 + '/asset.meta', '1' * 32 + '/pathname'})

    def test_guard_rejects_old_zero_byte_file_serialization_for_folder(self):
        identity = '1' * 32
        meta = ('fileFormatVersion: 2\nguid: ' + identity + '\nfolderAsset: yes\nDefaultImporter:\n').encode()
        path = self.root / 'old-folder.unitypackage'
        with path.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='archtemp.tar', mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w') as archive:
            directory = tarfile.TarInfo(identity)
            directory.type = tarfile.DIRTYPE
            archive.addfile(directory)
            for leaf, data in (('asset', b''), ('asset.meta', meta), ('pathname', b'Assets/ExampleFolder')):
                member = tarfile.TarInfo(identity + '/' + leaf)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        with self.assertRaisesRegex(ValueError, 'folder'):
            verify_unitypackage(path, {'Assets/ExampleFolder': (b'', meta)})

    def test_empty_file_exports_asset_and_guard_rejects_absent_asset(self):
        meta = b'fileFormatVersion: 2\nguid: ' + b'1' * 32 + b'\nDefaultImporter:\n'
        path = self.root / 'empty-file.unitypackage'
        write_unitypackage(path, {'Assets/empty.txt': (b'', meta)})
        with tarfile.open(path, 'r:gz') as archive:
            self.assertIn('1' * 32 + '/asset', archive.getnames())
        missing = self.root / 'missing-file.unitypackage'
        with path.open('rb') as original, tarfile.open(fileobj=original, mode='r:gz') as source, missing.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='archtemp.tar', mtime=0) as compressed, tarfile.open(fileobj=compressed, mode='w') as archive:
            for member in source:
                if member.name.endswith('/asset'): continue
                archive.addfile(member, source.extractfile(member) if member.isfile() else None)
        with self.assertRaisesRegex(ValueError, 'closure'):
            verify_unitypackage(missing, {'Assets/empty.txt': (b'', meta)})


if __name__ == '__main__':
    unittest.main()
