"""The GPU diagnostic ABI permits two exact UUID fields, never provider APIs."""
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
            path.write_text(source, encoding="utf-8")
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


if __name__ == "__main__":
    unittest.main()
