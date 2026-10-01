"""Real payload packaging and mutation tests; no synthetic native binaries."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/package/package_pc_demo.py'


class PcPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('pc_package', SCRIPT)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)
        cls.receipt = ROOT / 'out/pc-demo/native-inputs.json'

    def setUp(self):
        base = ROOT / 'out/pc-demo/package-tests'
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.addCleanup(self.temp.cleanup)
        self.destination = Path(self.temp.name) / 'package'

    def build(self):
        self.module.build(self.receipt, self.destination)

    def test_real_package_complete_and_repeatable(self):
        self.build()
        before = self.module.validate_package(self.destination)
        self.build()
        self.assertEqual(before, self.module.validate_package(self.destination))
        index = json.loads((self.destination / 'RuntimeData/index.json').read_text())
        self.assertEqual(7, len(index['files']))
        self.assertEqual({'windows-pc-cpu.json', 'windows-pc-directml.json'},
                         {Path(e['path']).name for e in index['files'] if e['path'].startswith('profiles/')})
        self.assertEqual(8, len(list((self.destination / 'Runtime/Plugins/x86_64').glob('*.dll'))))
        self.assertEqual({'HumanVision.Editor.asmdef', 'HumanVisionPcDemoBuilder.cs', 'HumanVisionModelInstaller.cs'},
                         {p.name for p in (self.destination / 'Editor').iterdir() if p.is_file() and p.suffix != '.meta'})
        self.assertFalse(list(self.destination.rglob('*GpuGate*')))

    def test_actual_model_tamper_rejected(self):
        self.build()
        path = self.destination / 'RuntimeData/modelpacks/rtmo-t-416/body.onnx'
        with path.open('r+b') as stream:
            stream.seek(1024); stream.write(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.module.validate_package(self.destination)

    def test_profile_crlf_changes_rejected(self):
        self.build()
        path = self.destination / 'RuntimeData/profiles/windows-pc-cpu.json'
        path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.module.validate_package(self.destination)

    def altered_receipt(self, change):
        receipt = json.loads(self.receipt.read_text())
        change(receipt)
        path = Path(self.temp.name) / 'receipt.json'
        path.write_text(json.dumps(receipt))
        return path

    def test_native_receipt_hash_tamper_rejected(self):
        path = self.altered_receipt(lambda r: r['files']['humanvision.dll'].update(sha256='0' * 64))
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.module.build(path, self.destination)
        self.assertFalse(self.destination.exists())

    def test_native_closure_missing_rejected(self):
        path = self.altered_receipt(lambda r: r['files'].pop('avcodec-61.dll'))
        with self.assertRaisesRegex(ValueError, 'closure'):
            self.module.build(path, self.destination)

    def test_receipt_path_traversal_rejected(self):
        path = self.altered_receipt(lambda r: r['files'].update({'../other.dll': r['files']['humanvision.dll']}))
        with self.assertRaises(ValueError):
            self.module.build(path, self.destination)

    def test_unsafe_destination_rejected(self):
        with self.assertRaisesRegex(ValueError, 'destination'):
            self.module.build(self.receipt, ROOT / 'upm/com.blazetc.humanvision')

    def test_native_not_pe_even_with_matching_hash_rejected(self):
        path = Path(self.temp.name) / 'humanvision.dll'
        shutil.copyfile(ROOT / 'profiles/windows-pc-cpu.json', path)
        receipt = self.altered_receipt(lambda r: r['files']['humanvision.dll'].update(
            source=str(path), bytes=path.stat().st_size, sha256=self.module.sha(path.read_bytes())))
        with self.assertRaisesRegex(ValueError, 'PE'):
            self.module.build(receipt, self.destination)

    def test_guid_collision_rejected(self):
        self.build()
        first = self.destination / 'RuntimeData/index.json.meta'
        second = self.destination / 'RuntimeData/profiles/windows-pc-cpu.json.meta'
        second.write_bytes(first.read_bytes())
        with self.assertRaisesRegex(ValueError, 'GUID'):
            self.module.validate_package(self.destination)

    def test_unknown_native_import_rejected_even_with_matching_hash(self):
        receipt = json.loads(self.receipt.read_text())
        source = Path(receipt['files']['humanvision.dll']['source'])
        data = source.read_bytes().replace(b'KERNEL32.dll\0', b'MISSING_.dll\0')
        self.assertNotEqual(data, source.read_bytes())
        path = Path(self.temp.name) / 'humanvision.dll'
        path.write_bytes(data)
        altered = self.altered_receipt(lambda r: r['files']['humanvision.dll'].update(
            source=str(path), bytes=len(data), sha256=self.module.sha(data)))
        with self.assertRaisesRegex(ValueError, 'closure'):
            self.module.build(altered, self.destination)

    def test_actual_native_architecture_mismatch_rejected(self):
        import struct
        receipt = json.loads(self.receipt.read_text())
        data = bytearray(Path(receipt['files']['humanvision.dll']['source']).read_bytes())
        offset = struct.unpack_from('<I', data, 60)[0]
        struct.pack_into('<H', data, offset + 4, 0xaa64)
        path = Path(self.temp.name) / 'humanvision.dll'
        path.write_bytes(data)
        altered = self.altered_receipt(lambda r: r['files']['humanvision.dll'].update(
            source=str(path), bytes=len(data), sha256=self.module.sha(data)))
        with self.assertRaisesRegex(ValueError, 'AMD64'):
            self.module.build(altered, self.destination)


if __name__ == '__main__':
    unittest.main()
