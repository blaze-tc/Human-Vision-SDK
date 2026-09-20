"""Dependency pins and rejection of untrusted build inputs (no network required)."""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ThirdPartyProvenanceTests(unittest.TestCase):
    def read_manifest(self, dependency):
        path = ROOT / "third_party" / dependency / "provenance.json"
        self.assertTrue(path.is_file(), f"Missing dependency provenance: {path}")
        return json.loads(path.read_text(encoding="utf-8"))

    def assert_file_hash(self, record):
        path = ROOT / record["path"]
        self.assertTrue(path.is_file(), str(path))
        self.assertEqual(record["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_ncnn_source_and_build_are_pinned(self):
        p = self.read_manifest("ncnn")
        self.assertEqual("20260526", p["version"])
        self.assertEqual("e54f7b1f88434e1d844ea0551b880a1cfb079ce1", p["source_commit"])
        self.assertEqual("https://github.com/Tencent/ncnn", p["source_url"])
        self.assertEqual(23279174, p["archive"]["size"])
        self.assertEqual("754659d6fe65545cf2ef4483ffb84526fea631f8764c44b150f1601d0fb4004b", p["archive"]["sha256"])
        self.assertEqual("https://github.com/Tencent/ncnn/releases/download/20260526/ncnn-20260526-full-source.zip", p["archive"]["url"])
        self.assert_file_hash(p["license"])
        self.assertEqual("third_party/ncnn/glslang-LICENSE.txt", p["bundled_licenses"][0]["path"])
        for license_record in p["bundled_licenses"]:
            self.assert_file_hash(license_record)
        self.assertEqual(1, len(p["patches"]))
        for patch in p["patches"]:
            self.assert_file_hash(patch)
            self.assertEqual(p["source_commit"], patch["base_commit"])
        self.assertEqual("arm64-v8a", p["android_abi"])
        self.assertEqual(26, p["android_api_level"])
        for key, value in {"NCNN_VULKAN": "ON", "NCNN_SHARED_LIB": "OFF", "NCNN_OPENMP": "OFF", "NCNN_BUILD_TOOLS": "OFF", "NCNN_BUILD_EXAMPLES": "OFF", "NCNN_BUILD_BENCHMARK": "OFF", "NCNN_BUILD_TESTS": "OFF", "NCNN_DISABLE_EXCEPTION": "OFF", "NCNN_DISABLE_RTTI": "OFF", "CMAKE_POSITION_INDEPENDENT_CODE": "ON", "ANDROID_STL": "c++_static"}.items():
            self.assertEqual(value, p["build_flags"][key], key)

    def test_unity_headers_retain_source_hashes_and_license(self):
        p = self.read_manifest("unity-plugin-api")
        self.assertEqual("2021.3.45f1", p["version"])
        self.assertEqual("D:/Developer/2021.3.45f1/Editor/Data/PluginAPI", p["source_path"])
        self.assertTrue(p["source_url"].startswith("https://"))
        self.assertEqual("Unity-Companion", p["license"]["id"])
        self.assert_file_hash(p["license"])
        self.assertEqual({"IUnityInterface.h", "IUnityGraphics.h", "IUnityGraphicsVulkan.h"}, {Path(h["path"]).name for h in p["headers"]})
        for header in p["headers"]:
            self.assert_file_hash(header)
            self.assertEqual(header["source_sha256"], header["sha256"])

    def test_setup_rejects_bad_size_before_extraction(self):
        script = ROOT / "tools/setup/prepare_ncnn_android.ps1"
        self.assertTrue(script.is_file(), "Missing deterministic dependency setup")
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "bad.zip"
            archive.write_bytes(b"not an archive")
            result = subprocess.run(["pwsh", "-NoProfile", "-File", str(script), "-ArchivePath", str(archive), "-Abi", "arm64-v8a", "-ApiLevel", "26"], capture_output=True, text=True, encoding="utf-8")
            self.assertNotEqual(0, result.returncode)
            self.assertIn("Archive size mismatch", result.stdout + result.stderr)

    def test_git_checkout_preserves_audited_upstream_bytes(self):
        records = []
        for dependency in ("ncnn", "unity-plugin-api"):
            manifest = self.read_manifest(dependency)
            records.append(manifest["license"])
            records.extend(manifest.get("headers", []))
            records.extend(manifest.get("bundled_licenses", []))
        for record in records:
            path = record["path"]
            with self.subTest(path=path):
                raw = (ROOT / path).read_bytes()
                blob = subprocess.check_output(["git", "hash-object", "-w", "--stdin"], input=raw, cwd=ROOT).decode().strip()
                checked_out = subprocess.check_output(["git", "-c", "core.autocrlf=true", "cat-file", "--filters", f"--path={path}", blob], cwd=ROOT)
                self.assertEqual(raw, checked_out, f"Git checkout changes audited bytes: {path}")

    def test_setup_rejects_same_size_wrong_hash_before_extraction(self):
        script = ROOT / "tools/setup/prepare_ncnn_android.ps1"
        self.assertTrue(script.is_file(), "Missing deterministic dependency setup")
        with tempfile.TemporaryDirectory() as temp:
            archive = Path(temp) / "bad.zip"
            with archive.open("wb") as stream:
                stream.truncate(23279174)
            result = subprocess.run(["pwsh", "-NoProfile", "-File", str(script), "-ArchivePath", str(archive)], capture_output=True, text=True, encoding="utf-8")
            self.assertNotEqual(0, result.returncode)
            self.assertIn("Archive SHA-256 mismatch", result.stdout + result.stderr)

    def test_explicit_download_suffix_archive_survives_success_and_failure(self):
        script = ROOT / "tools/setup/prepare_ncnn_android.ps1"
        with tempfile.TemporaryDirectory() as temp:
            fixture = Path(temp) / "fixture"
            copied_script = fixture / "tools/setup/prepare_ncnn_android.ps1"
            copied_script.parent.mkdir(parents=True)
            shutil.copyfile(script, copied_script)
            provenance_dir = fixture / "third_party/ncnn"
            provenance_dir.mkdir(parents=True)
            license_path = provenance_dir / "LICENSE"
            license_path.write_bytes(b"license")
            valid_bytes = b"caller-owned valid archive"
            provenance = {
                "version": "test", "archive": {
                    "name": "source.zip", "url": "https://invalid.example/source.zip",
                    "size": len(valid_bytes), "sha256": hashlib.sha256(valid_bytes).hexdigest(),
                },
                "license": {"path": "third_party/ncnn/LICENSE", "sha256": hashlib.sha256(b"license").hexdigest()},
                "bundled_licenses": [], "patches": [], "ndk_version": "test", "build_flags": {},
            }
            (provenance_dir / "provenance.json").write_text(json.dumps(provenance), encoding="utf-8")

            for cache_exists in (False, True):
                for succeeds in (False, True):
                    with self.subTest(cache_exists=cache_exists, succeeds=succeeds):
                        cache = fixture / "out/ncnn-test"
                        if cache.exists():
                            shutil.rmtree(cache)
                        if cache_exists:
                            cache.mkdir(parents=True)
                        case = fixture / f"case-{cache_exists}-{succeeds}"
                        case.mkdir()
                        archive = case / "caller-owned.download"
                        archive.write_bytes(valid_bytes)
                        command = ["pwsh", "-NoProfile", "-File", str(copied_script),
                            "-ArchivePath", str(archive)]
                        if succeeds:
                            command.append("-VerifyArchiveOnly")
                        else:
                            command.extend(["-AndroidNdk", str(case / "missing-ndk")])
                        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
                        self.assertEqual(succeeds, result.returncode == 0, result.stdout + result.stderr)
                        self.assertTrue(archive.is_file(), "Caller-owned archive was moved or deleted")
                        self.assertEqual(valid_bytes, archive.read_bytes())


if __name__ == "__main__":
    unittest.main()
