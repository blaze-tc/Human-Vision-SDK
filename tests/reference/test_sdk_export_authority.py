"""Exercise both real exporters against independent reviewed/stale payloads.

Native bytes here are tiny serialization fixtures, never runtime acceptance.
The regression is a generated package choosing stale canonical bytes instead
of the admitted SDK package, or losing its audit, dependency and metadata.
"""
import contextlib
import hashlib
import importlib
import io
import json
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/package'))
live = importlib.import_module('package_live_sdk')
upm = importlib.import_module('package_upm')
SETTINGS = 'Editor/HumanVisionAndroidBuildSettings.cs'
AUDIT_FALLBACK = 'Path.Combine(Directory.GetParent(Application.dataPath).FullName, "android-gpu-bridge-symbols.json")'
OFFLINE_FALLBACK = 'Path.Combine(Application.dataPath, "HumanVision", "android-gpu-bridge-symbols.json")'


def read_unitypackage(path):
    entries = {}
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            if member.isfile():
                guid, leaf = member.name.split('/', 1)
                entries.setdefault(guid, {})[leaf] = archive.extractfile(member).read()
    return {item['pathname'].decode(): item for item in entries.values()}


class SdkExportAuthorityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.sdk = self.root / 'upm/com.blazetc.humanvision'
        self.input = self.root / 'upm/com.blazetc.humanvision.input'
        self.source = self.root / 'unity/HumanVisionDemo/Assets/HumanVision'
        self.output = self.root / 'output'
        self.dest = self.root / 'generated-upm'
        self.expected = {}

        def put(relative, data):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            return path

        def sdk(relative, data):
            path = put('upm/com.blazetc.humanvision/' + relative, data)
            path.with_name(path.name + '.meta').write_text(live.metadata('UPM/' + relative))
            self.expected[relative] = data

        for name in ('HumanVisionAndroidBuildSettings.cs', 'HumanVisionAndroidRuntimeSettings.cs',
                     'HumanVisionAndroidRuntimeModeRegistry.cs', 'HumanVisionAndroidRuntimeBuildValidator.cs'):
            data = (ROOT / 'upm/com.blazetc.humanvision/Editor' / name).read_bytes()
            sdk('Editor/' + name, data)
            put('unity/HumanVisionDemo/Assets/HumanVision/Editor/' + name, b'// stale canonical validator\n')
        for relative in ('Editor/HumanVisionModelInstaller.cs', 'Editor/HumanVision.Editor.asmdef', 'README.md', 'package.json'):
            sdk(relative, (ROOT / 'upm/com.blazetc.humanvision' / relative).read_bytes())
        sdk('UPM_INSTALLATION.md', b'reviewed explicit input + SDK installation\n')
        sdk('.gitattributes', b'* -text\n')
        sdk('Runtime/HumanVision.Runtime.asmdef', b'{"name":"HumanVision.Runtime"}\n')
        sdk('Runtime/Demo/Current.cs', b'// current reviewed runtime\n')
        sdk('Runtime/Demo/Input/SharedSettingsPanel.prefab', b'reviewed shared prefab\n')
        (self.sdk / 'Runtime/Demo/Input.meta').write_bytes(
            b'fileFormatVersion: 2\nguid: 13131313131313131313131313131313\nfolderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n')
        for name in ('Camera', 'Video', 'Rtsp'):
            sdk('Samples~/UnifiedInput/HumanVision' + name + 'Demo.unity', (name + ' reviewed scene\n').encode())
        sdk('RuntimeData/profiles/cpu.json', b'{"profile":"reviewed-cpu"}\n')
        sdk('RuntimeData/modelpacks/example/body.onnx', b'exact admitted test model fixture')
        index = {'version': live.VERSION, 'files': [
            {'path': n.removeprefix('RuntimeData/'), 'sha256': hashlib.sha256(d).hexdigest()}
            for n, d in self.expected.items() if n.startswith('RuntimeData/')]}
        sdk('RuntimeData/index.json', (json.dumps(index) + '\n').encode())
        native = b'\x7fELF\x02\x01' + bytes(12) + b'\xb7\x00' + b'exact selected native fixture'
        sdk('Runtime/Plugins/Android/arm64-v8a/libhumanvision.so', native)
        sdk('android-gpu-bridge-symbols.json', json.dumps({'abi': 'arm64-v8a', 'api_level': 26,
            'ncnn_vulkan_symbols_verified': True, 'native_sha256': hashlib.sha256(native).hexdigest(), 'gpu_gate': False}).encode())
        put('upm/com.blazetc.humanvision.input/package.json', b'{"version":"0.1.0-preview.1"}')
        for name in ('libhumanvision_input.so', 'libavcodec.so', 'libavformat.so', 'libavutil.so', 'libswresample.so', 'libswscale.so'):
            path = put('upm/com.blazetc.humanvision.input/Runtime/Plugins/Android/arm64-v8a/' + name, ('input owner ' + name).encode())
            path.with_name(path.name + '.meta').write_text(live.metadata(path.name))
        # Complete legacy inputs so RED is an assertion on exported bytes,
        # rather than an unrelated missing-file exception.
        for name in ('humanvision.dll', 'humanvision_onnxruntime.dll', 'hv_dml.dll'):
            put('build/windows-live/bin/Release/' + name, b'legacy build fixture')
        put('build/android-live/bin/Release/libhumanvision.so', b'legacy android build fixture')
        put('out/live-deps/ort-android/lib/libonnxruntime.so', b'legacy ort fixture')
        for relative in ('out/hv-ort-dml/ORT_LICENSE', 'out/live-deps/ffmpeg-7.1/COPYING.LGPLv2.1',
                         'tools/reference/vendor/mmdetection/LICENSE', 'tools/reference/vendor/mmpose/LICENSE',
                         'out/directml-1.15.4/LICENSE.txt', 'out/directml-1.15.4/LICENSE-CODE.txt',
                         'out/directml-1.15.4/ThirdPartyNotices.txt', 'docs/SDK_040_USER_GUIDE.md',
                         'docs/UPM_INSTALLATION.md', 'docs/plans/040-v2/2026-09-10-humanvision-040-master-v2.md',
                         'tools/package/HumanVisionModelInstaller.cs', 'profiles/cpu.json', 'modelpacks/example/body.onnx'):
            put(relative, b'stale legacy fixture\n')

    def export(self):
        with patch.object(live, 'ROOT', self.root), patch.object(live, 'SOURCE', self.source), \
                patch.object(live, 'OUTPUT', self.output), patch.object(sys, 'argv', ['package_live_sdk.py']), \
                contextlib.redirect_stdout(io.StringIO()):
            live.main()
        return self.output / ('HumanVisionSDK-' + live.VERSION + '.unitypackage')

    def test_offline_export_preserves_reviewed_validator_native_data_and_docs(self):
        entries = read_unitypackage(self.export())
        checks = {SETTINGS: self.expected[SETTINGS].decode().replace(AUDIT_FALLBACK, OFFLINE_FALLBACK).encode(),
                  'README.md': self.expected['README.md'],
                  'android-gpu-bridge-symbols.json': self.expected['android-gpu-bridge-symbols.json']}
        for relative, expected in checks.items():
            with self.subTest(relative=relative):
                self.assertEqual(entries.get('Assets/HumanVision/' + relative, {}).get('asset'), expected)
        for relative in ('RuntimeData/index.json', 'RuntimeData/profiles/cpu.json', 'RuntimeData/modelpacks/example/body.onnx'):
            with self.subTest(relative=relative):
                self.assertEqual(entries.get('Assets/StreamingAssets/HumanVision/Runtime/' + relative.removeprefix('RuntimeData/'), {}).get('asset'), self.expected[relative])
        self.assertEqual(entries['Assets/Plugins/Android/arm64-v8a/libhumanvision.so']['asset'], self.expected['Runtime/Plugins/Android/arm64-v8a/libhumanvision.so'])
        for name in ('Camera', 'Video', 'Rtsp'):
            self.assertIn('Assets/HumanVision/Samples/UnifiedInput/HumanVision' + name + 'Demo.unity', entries)
        manifest = json.loads((self.output / 'asset-sha256.json').read_text())
        self.assertEqual(manifest, {name: hashlib.sha256(item['asset']).hexdigest() for name, item in entries.items()})
        self.assertEqual((self.output / 'README.md').read_bytes(), self.expected['README.md'])
        ffmpeg = [n for n in entries if Path(n).name.startswith(('libav', 'libsw'))]
        self.assertEqual(len(ffmpeg), 5)
        self.assertTrue(all(n.startswith('Assets/HumanVisionInput/') for n in ffmpeg))

    def test_upm_roundtrip_retains_exact_authority_and_dependency(self):
        package = self.export()
        with patch.object(upm, 'ROOT', self.root), patch.object(upm, 'DEST', self.dest), \
                patch.object(sys, 'argv', ['package_upm.py', str(package)]), contextlib.redirect_stdout(io.StringIO()):
            upm.main()
        for relative, data in self.expected.items():
            with self.subTest(relative=relative):
                self.assertEqual((self.dest / relative).read_bytes() if (self.dest / relative).is_file() else None, data)
                self.assertEqual((self.dest / (relative + '.meta')).read_bytes() if (self.dest / (relative + '.meta')).is_file() else None,
                                 (self.sdk / (relative + '.meta')).read_bytes())
        descriptor = json.loads((self.dest / 'package.json').read_text())
        self.assertEqual(descriptor['dependencies']['com.blazetc.humanvision.input'], '0.1.0-preview.1')
        self.assertFalse(any(p.name.startswith(('libav', 'libsw')) for p in self.dest.rglob('*.so')))
        with tarfile.open(self.output / ('com.blazetc.humanvision-' + live.VERSION + '.tgz')) as archive:
            self.assertEqual(archive.extractfile('package/' + SETTINGS).read(), self.expected[SETTINGS])
        manifest = json.loads((self.dest / 'asset-sha256.json').read_text())
        for relative, digest in manifest.items():
            self.assertEqual(hashlib.sha256((self.dest / relative).read_bytes()).hexdigest(), digest)

    def test_native_audit_mismatch_fails_before_export(self):
        (self.sdk / 'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so').write_bytes(b'changed native fixture')
        with self.assertRaisesRegex(ValueError, 'audit|SHA|hash'):
            self.export()
        self.assertFalse(self.output.exists())

    def test_upm_preserves_admitted_folder_metadata(self):
        package = self.export()
        entries = read_unitypackage(package)
        self.assertEqual(entries.get('Assets/HumanVision/Demo/Input', {}).get('asset.meta'),
                         (self.sdk / 'Runtime/Demo/Input.meta').read_bytes())
        with patch.object(upm, 'ROOT', self.root), patch.object(upm, 'DEST', self.dest), \
                patch.object(sys, 'argv', ['package_upm.py', str(package)]), contextlib.redirect_stdout(io.StringIO()):
            upm.main()
        self.assertEqual((self.dest / 'Runtime/Demo/Input.meta').read_bytes(),
                         (self.sdk / 'Runtime/Demo/Input.meta').read_bytes())

    def test_missing_reviewed_readme_does_not_fall_back_to_legacy_guide(self):
        (self.sdk / 'README.md').unlink()
        with self.assertRaises((FileNotFoundError, ValueError)):
            self.export()
        self.assertFalse(self.output.exists())

    def test_changed_validator_layout_fails_without_export(self):
        path = self.sdk / SETTINGS
        path.write_bytes(path.read_bytes().replace(AUDIT_FALLBACK.encode(), b'unsupported audit layout'))
        with self.assertRaisesRegex(ValueError, 'exactly once'):
            self.export()
        self.assertFalse(self.output.exists())

    def test_sdk_ffmpeg_duplicate_is_rejected(self):
        (self.sdk / 'Runtime/Plugins/Android/arm64-v8a/libavcodec.so').write_bytes(b'wrong second owner')
        with self.assertRaisesRegex(ValueError, 'solely by input'):
            self.export()
        self.assertFalse(self.output.exists())

    def test_dependency_mismatch_is_rejected_without_export(self):
        path = self.input / 'package.json'
        path.write_text('{"version":"unmatched-input-version"}')
        with self.assertRaisesRegex(ValueError, 'input dependency'):
            self.export()
        self.assertFalse(self.output.exists())

    def test_changed_offline_readme_is_rejected_before_upm_mutation(self):
        package = self.export()
        entries = read_unitypackage(package)
        entries['Assets/HumanVision/README.md']['asset'] = b'stale substituted guide'
        with tarfile.open(package, 'w:gz') as archive:
            for item in entries.values():
                guid = next(line.split(b':', 1)[1].strip().decode() for line in item['asset.meta'].splitlines()
                            if line.startswith(b'guid:'))
                for leaf, data in item.items():
                    info = tarfile.TarInfo(guid + '/' + leaf)
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
        with patch.object(upm, 'ROOT', self.root), patch.object(upm, 'DEST', self.dest), \
                patch.object(sys, 'argv', ['package_upm.py', str(package)]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(ValueError, 'admitted SDK authority'):
                upm.main()
        self.assertFalse(self.dest.exists())


if __name__ == '__main__':
    unittest.main()
