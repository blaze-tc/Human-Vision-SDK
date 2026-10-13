"""Packaging integrity tests; these do not simulate NPU inference."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from package_local_android_addon import read_verified_runtime


class RuntimeClosureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'model.bin').write_bytes(b'packaging-unit-test')
        self.rows = [dict(path='model.bin', sha256=hashlib.sha256(b'packaging-unit-test').hexdigest())]
        self.write_index()

    def write_index(self):
        data = json.dumps(dict(version='test', files=self.rows)).encode()
        (self.root/'index.json').write_bytes(data)
        (self.root/'staged-runtime.json').write_text(json.dumps(dict(
            schema_version=1, stage_id='local-qualified-runtime', local_evaluation_only=True,
            index_sha256=hashlib.sha256(data).hexdigest())))

    def test_only_indexed_files_exported(self):
        (self.root/'user-video.mp4').write_bytes(b'never-export')
        self.assertEqual(set(read_verified_runtime(self.root)), {'model.bin', 'index.json', 'staged-runtime.json'})

    def test_corrupt_asset_rejected(self):
        (self.root/'model.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'): read_verified_runtime(self.root)

    def test_traversal_rejected(self):
        self.rows[0]['path'] = '../model.bin'
        self.write_index()
        with self.assertRaisesRegex(ValueError, 'path'): read_verified_runtime(self.root)

    def test_duplicate_rejected(self):
        self.rows.append(dict(self.rows[0]))
        self.write_index()
        with self.assertRaisesRegex(ValueError, 'duplicate'): read_verified_runtime(self.root)

    def test_receipt_mismatch_rejected(self):
        (self.root/'index.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'receipt'): read_verified_runtime(self.root)


if __name__ == '__main__': unittest.main()
