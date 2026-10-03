"""Opaque configuration strings are data; provider APIs and assets stay guarded."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("public_surface_guard", ROOT / "tools/package/check_public_surface.py")
guard = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = guard
spec.loader.exec_module(guard)


class AndroidGpuPublicSurfaceTests(unittest.TestCase):
    def scan(self, relative, source):
        policy = json.loads((ROOT / "tests/contracts/public_surface_legacy_exceptions.json").read_text(encoding="utf-8"))
        exceptions = {(x["path"], x["token"], guard.normalized(x["line"]).rstrip(",;")) for x in policy["exceptions"]}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / relative
            path.parent.mkdir(parents=True)
            path.write_bytes(source.encode("utf-8"))
            return guard.scan_file(root, path, set(policy["internal_interop_files"]), exceptions)

    def test_exact_approved_android_uuid_declarations_are_accepted(self):
        for name in ("ncnn_device_uuid", "ncnn_driver_uuid"):
            with self.subTest(name=name):
                self.assertEqual(self.scan("native/include/humanvision/humanvision_android_gpu.h",
                    f"uint8_t {name}[HV_ANDROID_GPU_UUID_SIZE];"), [])

    def test_other_provider_details_and_modified_declarations_are_rejected(self):
        for source in ("void* ncnn_session;", "uint8_t ncnn_device_uuid[32];",
                       "uint8_t ncnn_driver_uuid[HV_ANDROID_GPU_UUID_SIZE]; void* ncnn_session;"):
            with self.subTest(source=source):
                self.assertTrue(self.scan("native/include/humanvision/humanvision_android_gpu.h", source))
        self.assertTrue(self.scan("native/include/humanvision/unrelated.h",
            "uint8_t ncnn_device_uuid[HV_ANDROID_GPU_UUID_SIZE];"))

    def test_provider_names_in_public_unity_api_remain_rejected(self):
        for relative in ("upm/com.blazetc.humanvision/Runtime/Camera.cs",
                         "unity/HumanVisionDemo/Assets/HumanVision/Runtime/Camera.cs"):
            with self.subTest(path=relative):
                self.assertTrue(self.scan(relative, "public object ncnn_device_uuid;"))

    def test_approved_profile_and_modelpack_string_ids_are_accepted(self):
        source = ('public string AnalysisProfileId = "android-ncnn-vulkan";\n'
                  'public string ModelPackId = "precision-t-26-ncnn-fp16";')
        self.assertEqual(self.scan("Runtime/Settings.cs", source), [])

    def test_provider_words_in_opaque_data_do_not_require_id_whitelisting(self):
        for token in ("RTMO", "RTMPose", "COCO", "Halpe", "QNN", "NNAPI",
                      "DirectML", "RKNN", "ncnn", "ONNX"):
            with self.subTest(token=token):
                self.assertEqual(self.scan("Runtime/Settings.cs",
                    f'public string ProfileId = "custom-{token}-profile";'), [])

    def test_normal_escaped_and_verbatim_strings_preserve_comment_markers(self):
        for source in (
            r'public string ProfileId = "https://profiles/ncnn";',
            r'public string ProfileId = "escaped\" ncnn \\ /* data */";',
            'public string ProfileId = @"ncnn ""quoted"" // data\nONNX /* data */";',
            r'public string Description = "ModelPath ONNX";',
        ):
            with self.subTest(source=source):
                self.assertEqual(self.scan("Runtime/Settings.cs", source), [])

    def test_comments_do_not_create_provider_or_asset_findings(self):
        source = ('public string ProfileId = "ordinary"; // ncnn "pose.onnx"\n'
                  '/* public ONNX Session;\npublic string ModelPath; */\n'
                  'public object /* ncnn */ Session;')
        self.assertEqual(self.scan("Runtime/Settings.cs", source), [])

    def test_provider_types_members_and_constructors_outside_strings_are_rejected(self):
        for source in (
            'public ncnn.Session Session;',
            'public object ncnnSession = "ordinary";',
            'public object Session = new ncnn.Session("ordinary");',
            'public object Session = Create("https://profiles/ncnn", new ncnn.Session());',
            'public string ProfileId = @"ordinary\n// data"; public ncnn.Session Session;',
            'public object /* data */ Session = new ncnn.Session();',
            'public object Session = $"{new ncnn.Session()}";',
            'public object Session = $"{Create("data", new ncnn.Session())}";',
            'public object Session = $"{Create("https://profiles/ordinary", new ncnn.Session())}";',
            'public object Session = $@"data\n{new ncnn.Session()}";',
            'public object Session = @$"data\n{new ncnn.Session()}";',
        ):
            with self.subTest(source=source):
                findings = self.scan("Runtime/Settings.cs", source)
                self.assertIn("ncnn", [finding.token for finding in findings])

    def test_every_forbidden_provider_identifier_remains_rejected(self):
        for token in ("RTMO", "RTMPose", "COCO", "Halpe", "QNN", "NNAPI",
                      "DirectML", "RKNN", "ncnn", "ONNX"):
            with self.subTest(token=token):
                findings = self.scan("Runtime/Settings.cs", f"public object {token}Session;")
                self.assertIn(token, [finding.token for finding in findings])

    def test_concrete_model_assets_and_model_config_members_remain_rejected(self):
        for source in (
            'public string ProfileId = "pose.onnx";',
            'private string Asset = @"C:\\models\\pose.param";',
            'public string ModelPath = "opaque-id";',
            'public string ModelFile;',
        ):
            with self.subTest(source=source):
                findings = self.scan("Runtime/Settings.cs", source)
                expected = "public-model-config" if "ModelPath" in source or "ModelFile" in source else "concrete-model-asset"
                self.assertIn(expected, [finding.token for finding in findings])

    def test_literal_only_array_entries_cannot_hide_model_assets(self):
        source = 'private string[] Assets = {\n"pose.onnx",\n@"pose.param"\n};'
        findings = self.scan("Runtime/Settings.cs", source)
        self.assertEqual([(f.line_number, f.token) for f in findings],
                         [(2, "concrete-model-asset"), (3, "concrete-model-asset")])

    def test_string_ending_cannot_hide_following_provider_identifier(self):
        for source in (
            'public string ProfileId = "ordinary"; public ncnn.Session Session;',
            'public string ProfileId = @"quoted ""data"""; public ncnn.Session Session;',
            r'public string ProfileId = "escaped\""; public ncnn.Session Session;',
            'public char Quote = \'"\'; public ncnn.Session Session;',
        ):
            with self.subTest(source=source):
                self.assertIn("ncnn", [finding.token for finding in self.scan("Runtime/Settings.cs", source)])

    def test_finding_line_and_original_text_are_preserved_after_multiline_literals(self):
        source = 'public string ProfileId = @"ncnn\nONNX";\npublic ncnn.Session Session;'
        findings = self.scan("Runtime/Settings.cs", source)
        self.assertEqual([(f.line_number, f.token, f.line) for f in findings],
                         [(3, "ncnn", "public ncnn.Session Session;")])

    def test_all_splitlines_separators_preserve_following_assets_and_diagnostics(self):
        for separator in ("\n", "\r", "\r\n", "\v", "\f", "\x1c", "\x1d",
                          "\x1e", "\x85", "\u2028", "\u2029"):
            for prefix, suffix in (('public string ProfileId = @"opaque', 'data";'),
                                   ('/* opaque', 'data */')):
                with self.subTest(separator=repr(separator), prefix=prefix):
                    source = (prefix + separator + suffix + '\n'
                              'private string Asset = "pose.onnx";\n'
                              'public ncnn.Session Session;')
                    findings = self.scan("Runtime/Settings.cs", source)
                    self.assertEqual([(f.line_number, f.token, f.line) for f in findings],
                        [(3, "concrete-model-asset", 'private string Asset = "pose.onnx";'),
                         (4, "ncnn", 'public ncnn.Session Session;')])

    def test_unicode_separator_cannot_truncate_the_trailing_model_asset(self):
        for separator in ("\x85", "\u2028", "\u2029"):
            for prefix, suffix in (('public string ProfileId = @"opaque', 'data";'),
                                   ('/* opaque', 'data */')):
                with self.subTest(separator=repr(separator), prefix=prefix):
                    source = prefix + separator + suffix + '\nprivate string Asset = "pose.onnx";'
                    findings = self.scan("Runtime/Settings.cs", source)
                    self.assertEqual([(f.line_number, f.token, f.line) for f in findings],
                        [(3, "concrete-model-asset", 'private string Asset = "pose.onnx";')])

    def test_interpolation_markers_in_comments_and_literal_data_are_ignored(self):
        for source in (
            'public object Session; // $"ncnn"',
            'public object Session; /* $@"ncnn" */',
            'public object Session; /* $@"ncnn\nONNX" */',
            'public string ProfileId = @"opaque $""ncnn"" data";',
            'public string ProfileId = @"opaque @$""ncnn"" data";',
            r'public string ProfileId = "opaque $\"ncnn\" data";',
        ):
            with self.subTest(source=source):
                self.assertEqual(self.scan("Runtime/Settings.cs", source), [])

    def test_unicode_separators_preserve_real_interpolation_expression_locations(self):
        for separator in ("\x85", "\u2028", "\u2029"):
            with self.subTest(separator=repr(separator)):
                source = ('public string ProfileId = @"opaque' + separator + 'data";\n'
                          'public object Session = $@"data' + separator + '{new ncnn.Session()}";')
                findings = self.scan("Runtime/Settings.cs", source)
                self.assertEqual([(f.line_number, f.token, f.line) for f in findings],
                    [(4, "ncnn", '{new ncnn.Session()}";')])

    def test_splitlines_separators_end_line_comments_before_provider_declarations(self):
        for separator in ("\n", "\r", "\r\n", "\v", "\f", "\x1c", "\x1d",
                          "\x1e", "\x85", "\u2028", "\u2029"):
            with self.subTest(separator=repr(separator)):
                source = '// opaque' + separator + 'public ncnn.Session Session;'
                findings = self.scan("Runtime/Settings.cs", source)
                self.assertEqual([(f.line_number, f.token, f.line) for f in findings],
                    [(2, "ncnn", 'public ncnn.Session Session;')])


if __name__ == "__main__":
    unittest.main()
