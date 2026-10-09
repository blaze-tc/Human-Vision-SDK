import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DeviceBundleTests(unittest.TestCase):
    def tool(self):
        path = ROOT / 'tools/models/rknn/run_device_probe.py'
        self.assertTrue(path.exists(), 'Missing hash-checked RK3588 test launcher')
        spec = importlib.util.spec_from_file_location('device_launcher', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_oneplus_is_not_accepted_as_rk3588(self):
        tool = self.tool()
        self.assertFalse(tool.is_rk3588(['SM8350', 'lahaina', 'OnePlus9Pro']))
        self.assertTrue(tool.is_rk3588(['rockchip', 'rk3588_t']))

    def test_bundle_path_traversal_is_rejected_before_upload(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            manifest = dict(schema_version=1, files={'../unrelated': '0' * 64})
            (path / 'bundle.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'path'):
                tool.validate_bundle(path)

    def test_changed_binary_is_rejected_before_upload(self):
        tool = self.tool()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'probe').write_bytes(b'changed')
            manifest = dict(schema_version=1, files={'probe': hashlib.sha256(b'original').hexdigest()})
            (path / 'bundle.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'SHA'):
                tool.validate_bundle(path)


if __name__ == '__main__':
    unittest.main()
