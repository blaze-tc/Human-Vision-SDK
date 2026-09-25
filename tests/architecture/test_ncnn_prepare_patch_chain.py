"""The audited ncnn patch chain must survive repeated prepare runs."""
import difflib
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/setup/prepare_ncnn_android.ps1"
NDK = Path("D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK")
VS = Path("D:/Microsoft Visual Studio")


def sha(data):
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless(shutil.which("pwsh") and NDK.is_dir() and VS.is_dir(), "Android prepare tools unavailable")
class NcnnPreparePatchChainTests(unittest.TestCase):
    def make_fixture(self, root):
        script = root / "tools/setup/prepare_ncnn_android.ps1"
        script.parent.mkdir(parents=True)
        shutil.copyfile(SCRIPT, script)
        provenance_dir = root / "third_party/ncnn"
        (provenance_dir / "patches").mkdir(parents=True)
        (provenance_dir / "LICENSE").write_bytes(b"fixture license\n")
        states = [
            b"first=old\nsecond=old\n",
            b"first=new\nsecond=old\n",
            b"first=new\nsecond=new\n",
        ]
        patches = []
        for index in (1, 2):
            name = f"third_party/ncnn/patches/{index:04d}-fixture.patch"
            diff = "".join(difflib.unified_diff(
                states[index - 1].decode().splitlines(keepends=True),
                states[index].decode().splitlines(keepends=True),
                fromfile="a/src/gpu.cpp", tofile="b/src/gpu.cpp",
            )).encode()
            (root / name).write_bytes(diff)
            patches.append({"path": name, "sha256": sha(diff), "files": [{
                "path": "src/gpu.cpp", "before_sha256": sha(states[index - 1]),
                "after_sha256": sha(states[index]),
            }]})
        archive = root / "fixture.zip"
        with zipfile.ZipFile(archive, "w") as zipped:
            zipped.writestr("src/gpu.cpp", states[0])
        provenance = {
            "version": "fixture", "archive": {"name": "source.zip", "url": "https://invalid.example/source.zip",
                "size": archive.stat().st_size, "sha256": sha(archive.read_bytes())},
            "license": {"path": "third_party/ncnn/LICENSE", "sha256": sha(b"fixture license\n")},
            "bundled_licenses": [], "patches": patches, "ndk_version": "23.1.7779620", "build_flags": {},
        }
        (provenance_dir / "provenance.json").write_text(json.dumps(provenance), encoding="utf-8")
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        subprocess.run(["git", "-C", str(root), "config", "core.autocrlf", "false"], check=True)
        return script, archive, states

    def prepare(self, script, archive):
        return subprocess.run(["pwsh", "-NoProfile", "-File", str(script),
            "-ArchivePath", str(archive), "-VerifySourceOnly", "-Jobs", "1"],
            cwd=script.parents[2], capture_output=True, text=True, encoding="utf-8")

    def test_two_patch_chain_reprepare_and_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script, archive, states = self.make_fixture(root)
            source = root / "out/ncnn-fixture/source/src/gpu.cpp"
            first = self.prepare(script, archive)
            self.assertEqual(0, first.returncode, first.stdout + first.stderr)
            self.assertEqual(states[2], source.read_bytes())
            source.write_bytes(states[1])
            intermediate = self.prepare(script, archive)
            self.assertEqual(0, intermediate.returncode, intermediate.stdout + intermediate.stderr)
            self.assertEqual(states[2], source.read_bytes())
            again = self.prepare(script, archive)
            self.assertEqual(0, again.returncode, again.stdout + again.stderr)
            self.assertEqual(states[2], source.read_bytes())
            source.write_bytes(b"first=tampered\nsecond=new\n")
            tampered = self.prepare(script, archive)
            self.assertNotEqual(0, tampered.returncode)
            self.assertIn("Source/patch SHA-256 mismatch", tampered.stdout + tampered.stderr)


if __name__ == "__main__":
    unittest.main()
