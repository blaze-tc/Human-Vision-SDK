"""Red/green archive and immutable source guards; no inference acceptance claims."""
import gzip
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

from package_release_snapshot import (file_hashes, sha256, verify_file_closure,
                                      write_tgz, verify_tgz, validate_guids,
                                      verify_unitypackage, write_unitypackage, input_path)


class SnapshotGuards(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

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
