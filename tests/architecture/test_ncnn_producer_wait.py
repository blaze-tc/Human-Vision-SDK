"""Guard producer synchronization against pinned ncnn implicit extraction submits.

This tests submission grouping and failure lifetime, not Vulkan correctness or
device performance. Android/ncnn cannot be instantiated by the Windows tests.
The actual production bodies are compiled, so removing a wait/release or
quarantine changes observed behavior instead of only changing source text.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cl"), "Requires the pinned VS developer environment")
class SubmissionBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(prefix="ncnn-crop-")
        folder = Path(cls.folder.name)
        source = (ROOT / "runtime/plugins/backend/ncnn/ncnn_android_session.cpp").read_text()
        bodies = source[source.index("HV_Result AndroidSession::Run("):source.index("HV_Result AndroidSession::Info(")]
        harness = (ROOT / "tests/architecture/fixtures/ncnn_producer_wait.cpp").read_text()
        (folder / "test.cpp").write_text(harness.replace("// PRODUCTION_BODIES", bodies))
        built = subprocess.run(["cl", "/nologo", "/std:c++17", "/EHsc", "test.cpp", "/Fe:test.exe"],
                               cwd=folder, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        cls.exe = folder / "test.exe"

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def check(self, case):
        result = subprocess.run([str(self.exe), case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_internal(self):
        self.check("internal")

    def test_internal_reset(self):
        self.check("internal_reset")

    def test_import(self):
        self.check("import")

    def test_rgb(self):
        self.check("rgb")

    def test_handoff_preprocess(self):
        self.check("handoff_preprocess")

    def test_raw(self):
        self.check("raw")

    def test_signed(self):
        self.check("signed")

    def test_handoff(self):
        self.check("handoff")

    def test_legacy(self):
        self.check("legacy")

    def test_fp16(self):
        self.check("fp16")

    def test_preprocess(self):
        self.check("preprocess")

    def test_input(self):
        self.check("input")

    def test_extract(self):
        self.check("extract")

    def test_submit(self):
        self.check("submit")

    def test_reset(self):
        self.check("reset")

    def test_cleanup(self):
        self.check("cleanup")

    def test_release(self):
        self.check("release")


if __name__ == "__main__":
    unittest.main()
