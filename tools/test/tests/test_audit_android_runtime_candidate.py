"""Controlled parser/closure/output tests; these do not establish device support."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "audit_android_runtime_candidate.py"
HEADER = """ELF Header:
  Class:                             ELF64
  Machine:                           AArch64
Displaying notes found in: .note.android.ident
  description data: 1a 00 00 00 00 00 00 00
Dynamic section:
  0x0000000000000001 (NEEDED) Shared library: [libandroid.so]
  0x0000000000000001 (NEEDED) Shared library: [libvulkan.so]
  0x0000000000000001 (NEEDED) Shared library: [liblog.so]
  0x0000000000000001 (NEEDED) Shared library: [libonnxruntime.so]
"""
REQUIRED_SYMBOLS = (
    "ncnn::VulkanDevice::VulkanDevice()",
    "ncnn::VkCompute::record_import_android_hardware_buffer(void*, VkImageLayout, unsigned int, unsigned int)",
    "vkCreateInstance",
    "humanvision::runtime::rknn::(anonymous namespace)::AndroidVendor::Initialize()",
    "humanvision::runtime::rknn::(anonymous namespace)::AndroidVendor::InputsSet()",
    "humanvision::runtime::rknn::(anonymous namespace)::AndroidVendor::Run()",
    "humanvision::runtime::rknn::(anonymous namespace)::AndroidVendor::OutputsGet()",
    "humanvision::runtime::rknn::(anonymous namespace)::AndroidLoader::Load()",
    "humanvision::runtime::rknn::MakePlatformLoader()",
)
FILENAMES = (
    "android-gpu-bridge-symbols.json",
    "android-cpu-runtime-symbols.json",
    "android-neural-runtime-symbols.json",
)
RKNN_RUNTIME_EXPORTS = frozenset((
    "rknn_init", "rknn_destroy", "rknn_query", "rknn_set_core_mask",
    "rknn_inputs_set", "rknn_run", "rknn_outputs_get", "rknn_outputs_release",
    "rknn_create_mem", "rknn_destroy_mem", "rknn_set_io_mem", "rknn_mem_sync",
))
ORT_VERSIONED_API = "OrtGetApiBase@VERS_1.23.0"


def symbols_without(*removed):
    return "\n".join("0000000000010000 T " + name for name in REQUIRED_SYMBOLS
                     if not any(part in name for part in removed))


class AndroidCandidateAuditTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.is_file(), "Candidate audit implementation is missing")
        spec = importlib.util.spec_from_file_location("candidate_audit_test", SCRIPT)
        self.audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.audit)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_api26_arm64_note_and_actual_needed_are_parsed(self):
        parsed = self.audit.parse_header(HEADER)
        self.assertEqual(26, parsed["api_level"])
        self.assertIn("libonnxruntime.so", parsed["needed"])

    def test_wrong_architecture_is_rejected(self):
        with self.assertRaises(self.audit.AuditError):
            self.audit.parse_header(HEADER.replace("AArch64", "ARM"))

    def test_elf32_is_rejected(self):
        with self.assertRaises(self.audit.AuditError):
            self.audit.parse_header(HEADER.replace("ELF64", "ELF32"))

    def test_missing_android_note_is_rejected(self):
        with self.assertRaises(self.audit.AuditError):
            self.audit.parse_header(HEADER.replace(".note.android.ident", ".note.other"))

    def test_wrong_android_api_is_rejected(self):
        with self.assertRaises(self.audit.AuditError):
            self.audit.parse_header(HEADER.replace("1a 00 00 00", "1b 00 00 00"))

    def test_complete_ncnn_and_vendor_symbols_are_accepted(self):
        self.audit.verify_sdk_symbols(symbols_without())

    def test_rknn_off_query_and_factory_still_fail_without_android_vendor(self):
        stub = symbols_without("AndroidVendor::", "AndroidLoader::")
        stub += "\n0000000000010000 T HV_QueryRknnPluginV1\n"
        stub += "0000000000010000 T humanvision::runtime::rknn::CreateRknnSession()\n"
        with self.assertRaises(self.audit.AuditError):
            self.audit.verify_sdk_symbols(stub)

    def test_each_required_symbol_family_is_required(self):
        for name in REQUIRED_SYMBOLS:
            with self.subTest(symbol=name), self.assertRaises(self.audit.AuditError):
                self.audit.verify_sdk_symbols(symbols_without(name))

    def test_api23_import_export_and_twelve_vendor_exports_are_accepted(self):
        self.audit.verify_runtime_symbols(
            {ORT_VERSIONED_API}, {ORT_VERSIONED_API}, RKNN_RUNTIME_EXPORTS)

    def test_sdk_missing_versioned_api23_import_is_rejected(self):
        for imports in [set(), {"OrtGetApiBase"}, {"OrtGetApiBase@VERS_1.24.0"}]:
            with self.subTest(imports=imports), self.assertRaises(self.audit.AuditError):
                self.audit.verify_runtime_symbols(
                    imports, {ORT_VERSIONED_API}, RKNN_RUNTIME_EXPORTS)

    def test_ort_missing_versioned_api23_export_is_rejected(self):
        for exports in [set(), {"OrtGetApiBase"}, {"OrtGetApiBase@VERS_1.24.0"}]:
            with self.subTest(exports=exports), self.assertRaises(self.audit.AuditError):
                self.audit.verify_runtime_symbols(
                    {ORT_VERSIONED_API}, exports, RKNN_RUNTIME_EXPORTS)

    def test_each_of_twelve_rknn_vendor_exports_is_required(self):
        for name in sorted(RKNN_RUNTIME_EXPORTS):
            with self.subTest(export=name), self.assertRaises(self.audit.AuditError):
                self.audit.verify_runtime_symbols(
                    {ORT_VERSIONED_API}, {ORT_VERSIONED_API},
                    RKNN_RUNTIME_EXPORTS - {name})

    def test_pinned_hash_is_calculated_from_real_bytes(self):
        path = self.root / "runtime.so"
        path.write_bytes(b"actual runtime bytes")
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertEqual(expected, self.audit.verify_pinned_hash(path, expected))

    def test_bad_pinned_hash_is_rejected(self):
        path = self.root / "runtime.so"
        path.write_bytes(b"modified runtime bytes")
        with self.assertRaises(self.audit.AuditError):
            self.audit.verify_pinned_hash(path, "0" * 64)

    def test_missing_pinned_runtime_is_rejected(self):
        with self.assertRaises((self.audit.AuditError, FileNotFoundError)):
            self.audit.verify_pinned_hash(self.root / "missing.so", "0" * 64)

    def _closure(self, dependencies, symbol_map, needed_map):
        plugin = self.root / "plugins"
        system = self.root / "api26"
        plugin.mkdir()
        system.mkdir()
        library = plugin / "libhumanvision.so"
        library.touch()
        for name in symbol_map:
            (plugin / name).touch()
        return self.audit.closure.validate_dynamic_closure(
            library, dependencies, [plugin], [system],
            lambda path: symbol_map.get(path.name, (set(), set())),
            lambda path: needed_map.get(path.name, []))

    def test_closure_rejects_missing_actual_packaged_dependency(self):
        with self.assertRaisesRegex(AssertionError, "Unresolved dynamic dependency"):
            self._closure(["libabsent.so"], {}, {})

    def test_closure_rejects_unresolved_transitive_strong_import(self):
        with self.assertRaisesRegex(AssertionError, "Unresolved strong imports"):
            self._closure(["libdep.so"], {
                "libhumanvision.so": ({"entry"}, {"dep"}),
                "libdep.so": ({"dep"}, {"missing_api27_function"}),
            }, {})

    def test_closure_accepts_real_dependency_exports(self):
        count = self._closure(["libdep.so"], {
            "libhumanvision.so": ({"entry"}, {"dep"}),
            "libdep.so": ({"dep"}, set()),
        }, {})
        self.assertEqual(1, count)

    def documents(self):
        return {name: {"abi": "arm64-v8a", "api_level": 26,
                       "local_evaluation_only": True,
                       "device_performance_verified": False,
                       "distribution_qualified": False,
                       "gpu_gate": False}
                for name in FILENAMES}

    def test_three_audits_preserve_non_hardware_qualification_flags(self):
        output = self.root / "new-audit"
        self.audit.write_audits(output, self.documents())
        self.assertEqual(set(FILENAMES), {path.name for path in output.iterdir()})
        for name in FILENAMES:
            actual = json.loads((output / name).read_text(encoding="utf-8"))
            self.assertTrue(actual["local_evaluation_only"])
            for flag in ["device_performance_verified", "distribution_qualified", "gpu_gate"]:
                self.assertFalse(actual[flag])

    def test_existing_output_is_rejected_without_modification(self):
        output = self.root / "existing"
        output.mkdir()
        sentinel = output / "keep.txt"
        sentinel.write_text("keep", encoding="utf-8")
        with self.assertRaises((self.audit.AuditError, FileExistsError)):
            self.audit.write_audits(output, self.documents())
        self.assertEqual([sentinel], list(output.iterdir()))
        self.assertEqual("keep", sentinel.read_text(encoding="utf-8"))

    def test_escaping_output_name_is_rejected_before_any_write(self):
        output = self.root / "new-audit"
        docs = self.documents()
        docs["../escape.json"] = {}
        with self.assertRaises(self.audit.AuditError):
            self.audit.write_audits(output, docs)
        self.assertFalse(output.exists())
        self.assertFalse((self.root / "escape.json").exists())

    def test_missing_audit_document_is_rejected_before_any_write(self):
        output = self.root / "new-audit"
        docs = self.documents()
        del docs[FILENAMES[0]]
        with self.assertRaises(self.audit.AuditError):
            self.audit.write_audits(output, docs)
        self.assertFalse(output.exists())

    def test_duplicate_equivalent_filename_is_rejected_before_any_write(self):
        output = self.root / "new-audit"
        docs = self.documents()
        docs[FILENAMES[0].upper()] = dict(docs[FILENAMES[0]])
        with self.assertRaises(self.audit.AuditError):
            self.audit.write_audits(output, docs)
        self.assertFalse(output.exists())

    def test_missing_qualification_flag_is_rejected_before_any_write(self):
        for flag in ["local_evaluation_only", "device_performance_verified",
                     "distribution_qualified", "gpu_gate"]:
            with self.subTest(flag=flag):
                output = self.root / flag
                docs = self.documents()
                del docs[FILENAMES[0]][flag]
                with self.assertRaises(self.audit.AuditError):
                    self.audit.write_audits(output, docs)
                self.assertFalse(output.exists())

    def test_promoted_qualification_flag_is_rejected_before_any_write(self):
        for flag in ["local_evaluation_only", "device_performance_verified",
                     "distribution_qualified", "gpu_gate"]:
            with self.subTest(flag=flag):
                output = self.root / flag
                docs = self.documents()
                docs[FILENAMES[0]][flag] = flag != "local_evaluation_only"
                with self.assertRaises(self.audit.AuditError):
                    self.audit.write_audits(output, docs)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
