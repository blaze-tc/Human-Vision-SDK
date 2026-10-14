"""Local addon byte/GUID closure tests; no model or inference fixture is used."""
import hashlib
import json
from pathlib import Path
import tarfile
import unittest
from unittest.mock import patch

import package_main_cpu_android_addon as addon
import stage_main_cpu_yolo as staging
from package_live_sdk import metadata
import test_main_cpu_yolo_stage as stage_fixture


def digest(data):
    return hashlib.sha256(data).hexdigest()


class AddonTests(unittest.TestCase):
    def setUp(self):
        fixtures = {
            'INPUT_SHA256': b'packaging-only-input-native',
            'WINDOWS_SHA256': b'packaging-only-windows-native',
            'ORT_SHA256': b'packaging-only-ort-native',
            'VENDOR_SHA256': b'packaging-only-rknn-runtime',
        }
        for key, data in fixtures.items():
            patched = patch.object(staging, key, digest(data))
            patched.start()
            self.addCleanup(patched.stop)
        self.fixture = stage_fixture.StageTests('test_stage_changes_only_cpu_profile_and_adds_complete_pack')
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.run_stage()
        root = self.fixture.root
        self.sdk, self.input = root/'sdk', root/'input'
        self.vendor = root/'librknnrt.so'
        self.vendor.write_bytes(fixtures['VENDOR_SHA256'])
        self.output = root/'addon'
        libs = {
            self.sdk: {
                'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so': self.fixture.native.read_bytes(),
                'Runtime/Plugins/Android/arm64-v8a/libonnxruntime.so': fixtures['ORT_SHA256'],
                'Runtime/Plugins/x86_64/humanvision.dll': fixtures['WINDOWS_SHA256'],
            },
            self.input: {
                'Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so': fixtures['INPUT_SHA256'],
            },
        }
        for package, rows in libs.items():
            for name, data in rows.items():
                path = package/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                Path(str(path)+'.meta').write_text(metadata('UPM/'+package.name+'/'+name))
            version = '0.4.0-preview.8' if package == self.sdk else '0.1.0-preview.5'
            (package/'package.json').write_text(json.dumps({'version': version}))
        self.template = addon.ROOT/'tools/package/LocalCpuAndroidSetup.cs.txt'

    def build(self):
        return addon.build(self.fixture.output, self.sdk, self.input, self.vendor,
                           self.output, '0.4.0-preview.8', '0.1.0-preview.5')

    def test_full_stage_and_vendor_export_with_no_sdk_input_binary_duplicates(self):
        result = self.build()
        self.assertEqual(result['runtime_file_count'], 52)
        self.assertFalse(result['distribution_qualified'])
        self.assertFalse(result['hardware_fps_acceptance'])
        archive = Path(result['archive'])
        assets, guids = {}, set()
        with tarfile.open(archive, 'r:gz') as pack:
            for member in pack:
                if member.name.endswith('/pathname'):
                    guid = member.name.split('/')[0]
                    self.assertNotIn(guid, guids)
                    guids.add(guid)
                    path = pack.extractfile(member).read().decode()
                    try:
                        assets[path] = pack.extractfile(guid+'/asset').read()
                    except KeyError:
                        pass
        self.assertEqual(assets['Assets/StreamingAssets/HumanVision/Runtime/modelpacks/yolov8n-pose-512x288-ort/body.onnx'],
                         self.fixture.model.read_bytes())
        self.assertEqual(assets['Assets/Plugins/Android/arm64-v8a/librknnrt.so'], self.vendor.read_bytes())
        self.assertEqual(sum(path.startswith('Assets/StreamingAssets/HumanVision/Runtime/') for path in assets), 54)
        self.assertFalse(any(Path(path).name in ('libhumanvision.so', 'libhumanvision_input.so', 'libonnxruntime.so')
                             for path in assets))
        self.assertEqual(digest(archive.read_bytes()), result['archive_sha256'])
        self.assertTrue((self.output/'SHA256SUMS.txt').is_file())

    def test_export_rejects_mismatched_current_sdk_before_output(self):
        (self.sdk/'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so').write_bytes(b'wrong-sdk')
        with self.assertRaisesRegex(ValueError, 'SDK.*hash|SDK.*SHA'): self.build()
        self.assertFalse(self.output.exists())

    def test_export_rejects_mismatched_ort_before_output(self):
        (self.sdk/'Runtime/Plugins/Android/arm64-v8a/libonnxruntime.so').write_bytes(b'wrong-ort')
        with self.assertRaisesRegex(ValueError, 'ORT.*hash|ORT.*SHA'): self.build()
        self.assertFalse(self.output.exists())

    def test_export_rejects_mismatched_input_before_output(self):
        (self.input/'Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so').write_bytes(b'wrong-input')
        with self.assertRaisesRegex(ValueError, 'Input.*hash|Input.*SHA'): self.build()
        self.assertFalse(self.output.exists())

    def test_export_rejects_mismatched_vendor_before_output(self):
        self.vendor.write_bytes(b'wrong-rknn')
        with self.assertRaisesRegex(ValueError, 'RKNN.*hash|RKNN.*SHA'): self.build()
        self.assertFalse(self.output.exists())

    def test_export_requires_matching_supplied_package_version(self):
        (self.sdk/'package.json').write_text('{"version":"0.4.0-preview.7"}')
        with self.assertRaisesRegex(ValueError, 'version'): self.build()
        self.assertFalse(self.output.exists())

    def test_duplicate_dependency_guids_rejected(self):
        path = self.sdk/'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so.meta'
        other = self.input/'Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so.meta'
        other.write_bytes(path.read_bytes())
        with self.assertRaisesRegex(ValueError, 'GUID'): self.build()
        self.assertFalse(self.output.exists())

    def test_missing_meta_rejected(self):
        (self.sdk/'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so.meta').unlink()
        with self.assertRaisesRegex(ValueError, 'meta|missing|Missing'): self.build()
        self.assertFalse(self.output.exists())

    def test_existing_output_never_overwritten(self):
        self.output.mkdir()
        keep = self.output/'keep'
        keep.write_bytes(b'keep')
        with self.assertRaisesRegex(ValueError, 'fresh|new output'): self.build()
        self.assertEqual(keep.read_bytes(), b'keep')


if __name__ == '__main__': unittest.main()
