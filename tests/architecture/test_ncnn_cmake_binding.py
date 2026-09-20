"""CMake must bind imported ncnn/glslang targets to the verified receipt."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NDK = Path("D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK")
VS = Path("D:/Microsoft Visual Studio")
CMAKE = VS / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA = VS / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"

TARGETS = {
    "ncnn": "libncnn.a",
    "glslang::SPIRV": "libSPIRV.a",
    "glslang::OSDependent": "libOSDependent.a",
    "glslang::glslang": "libglslang.a",
    "glslang::MachineIndependent": "libMachineIndependent.a",
    "glslang::GenericCodeGen": "libGenericCodeGen.a",
    "glslang::glslang-default-resource-limits": "libglslang-default-resource-limits.a",
}


@unittest.skipUnless(CMAKE.is_file() and NINJA.is_file() and NDK.is_dir(), "Android CMake toolchain unavailable")
class NcnnCmakeBindingTests(unittest.TestCase):
    def make_root(self, base, target_override=None):
        install = base / "install"
        config_dir = install / "lib/cmake/ncnn"
        config_dir.mkdir(parents=True)
        (install / "include/ncnn").mkdir(parents=True)
        libraries = []
        lines = [
            "set(NCNN_VERSION 20260526)", "set(NCNN_VULKAN ON)",
            "set(NCNN_SHARED_LIB OFF)", "set(NCNN_OPENMP OFF)",
        ]
        for target, filename in TARGETS.items():
            path = install / "lib" / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("verified:" + filename).encode())
            libraries.append({
                "path": "lib/" + filename,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            })
            location = target_override if target == "ncnn" and target_override else path
            lines.extend([
                f"add_library({target} STATIC IMPORTED)",
                f'set_target_properties({target} PROPERTIES IMPORTED_LOCATION_RELEASE "{location.as_posix()}")',
            ])
        (config_dir / "ncnnConfig.cmake").write_text("\n".join(lines), encoding="utf-8")
        provenance_hash = hashlib.sha256((ROOT / "third_party/ncnn/provenance.json").read_bytes()).hexdigest()
        (base / "build-receipt.json").write_text(json.dumps({
            "provenance_sha256": provenance_hash,
            "libraries": libraries,
        }), encoding="utf-8")
        return install

    def make_stale_config(self, base):
        config = base / "lib/cmake/ncnn"
        config.mkdir(parents=True)
        stale = base / "lib/libncnn.a"
        stale.parent.mkdir(parents=True, exist_ok=True)
        stale.write_bytes(b"stale")
        (config / "ncnnConfig.cmake").write_text("\n".join([
            "set(NCNN_VERSION 20260526)", "set(NCNN_VULKAN ON)",
            "set(NCNN_SHARED_LIB OFF)", "set(NCNN_OPENMP OFF)",
            "add_library(ncnn STATIC IMPORTED)",
            f'set_target_properties(ncnn PROPERTIES IMPORTED_LOCATION_RELEASE "{stale.as_posix()}")',
        ]), encoding="utf-8")
        return config

    def map_release_to_debug(self, install, location):
        config = install / "lib/cmake/ncnn/ncnnConfig.cmake"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("\nset_target_properties(ncnn PROPERTIES\n")
            stream.write(f'  IMPORTED_LOCATION_DEBUG "{location.as_posix()}"\n')
            stream.write("  MAP_IMPORTED_CONFIG_RELEASE Debug)\n")

    def use_generic_location(self, install, location):
        config = install / "lib/cmake/ncnn/ncnnConfig.cmake"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("\nset_property(TARGET ncnn PROPERTY IMPORTED_LOCATION_RELEASE)\n")
            stream.write(f'set_target_properties(ncnn PROPERTIES IMPORTED_LOCATION "{location.as_posix()}")\n')

    def use_unmapped_debug_fallback(self, install, location):
        config = install / "lib/cmake/ncnn/ncnnConfig.cmake"
        with config.open("a", encoding="utf-8") as stream:
            stream.write("\nset_property(TARGET ncnn PROPERTY IMPORTED_LOCATION_RELEASE)\n")
            stream.write("set_property(TARGET ncnn PROPERTY IMPORTED_CONFIGURATIONS DEBUG)\n")
            stream.write(f'set_target_properties(ncnn PROPERTIES IMPORTED_LOCATION_DEBUG "{location.as_posix()}")\n')

    def assert_release_consumer_selects(self, temp, install, expected_location, extra_args=None):
        source = temp / "consumer"
        build = temp / "consumer-build"
        source.mkdir()
        (source / "consumer.cpp").write_text("extern int hv_ncnn_binding_fixture;\n", encoding="utf-8")
        (source / "CMakeLists.txt").write_text("\n".join([
            "cmake_minimum_required(VERSION 3.24)",
            "project(NcnnImportedSelection LANGUAGES CXX)",
            f'find_package(ncnn REQUIRED CONFIG PATHS "{(install / "lib/cmake/ncnn").as_posix()}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)',
            "add_library(consumer SHARED consumer.cpp)",
            "target_link_libraries(consumer PRIVATE ncnn)",
        ]), encoding="utf-8")
        command = [str(CMAKE), "--fresh", "-S", str(source), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={NINJA}",
            f"-DCMAKE_TOOLCHAIN_FILE={NDK / 'build/cmake/android.toolchain.cmake'}",
            "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        command.extend(extra_args or [])
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        generated = (build / "build.ninja").read_text(encoding="utf-8")
        self.assertIn(expected_location.as_posix(), generated)

    def add_receipt_library(self, install, filename, content):
        path = install / "lib" / filename
        path.write_bytes(content)
        receipt_path = install.parent / "build-receipt.json"
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        receipt["libraries"].append({
            "path": "lib/" + filename,
            "sha256": hashlib.sha256(content).hexdigest(),
        })
        receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
        return path

    def configure(self, build, ncnn_root, stale_dir=None, extra_args=None):
        command = [str(CMAKE), "--fresh", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={NINJA}",
            f"-DCMAKE_TOOLCHAIN_FILE={NDK / 'build/cmake/android.toolchain.cmake'}",
            "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-26", "-DANDROID_STL=c++_static",
            "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF", "-DHV_ENABLE_RTSP=ON",
            f"-DHV_NCNN_ROOT={ncnn_root}",
            f"-DHV_ONNXRUNTIME_ROOT={ROOT / 'out/live-deps/ort-android'}",
            f"-DHV_FFMPEG_INCLUDE={ROOT / 'out/live-deps/ffmpeg-headers'}",
            f"-DHV_FFMPEG_LIB_DIR={ROOT / 'out/live-deps/ffmpeg-android'}",
        ]
        if stale_dir:
            command.append(f"-Dncnn_DIR={stale_dir}")
        command.extend(extra_args or [])
        return subprocess.run(command, capture_output=True, text=True, encoding="utf-8")

    def test_stale_ncnn_dir_is_reset_to_verified_root(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            expected = self.make_root(temp / "expected")
            stale = self.make_stale_config(temp / "stale")
            build = temp / "build"
            result = self.configure(build, expected, stale)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            selected = (expected / "lib/cmake/ncnn").as_posix()
            self.assertIn(f"ncnn_DIR:PATH={selected}", cache)
            self.assertNotIn(f"ncnn_DIR:PATH={stale.as_posix()}", cache)

    def test_imported_location_not_in_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            outside = temp / "outside/libncnn.a"
            outside.parent.mkdir(parents=True)
            outside.write_bytes(b"not receipt verified")
            expected = self.make_root(temp / "expected", outside)
            result = self.configure(temp / "build", expected)
            self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("not listed in the verified ncnn receipt", result.stdout + result.stderr)

    def test_mapped_release_location_not_in_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            outside = temp / "outside/libncnn.a"
            outside.parent.mkdir(parents=True)
            outside.write_bytes(b"mapped outside receipt")
            expected = self.make_root(temp / "expected")
            self.map_release_to_debug(expected, outside)
            self.assert_release_consumer_selects(temp, expected, outside)
            result = self.configure(temp / "build", expected)
            self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("not listed in the verified ncnn receipt", result.stdout + result.stderr)

    def test_mapped_release_location_in_receipt_is_accepted_and_linked(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            expected = self.make_root(temp / "expected")
            mapped = self.add_receipt_library(expected, "libncnn-mapped.a", b"verified mapped ncnn")
            self.map_release_to_debug(expected, mapped)
            build = temp / "build"
            result = self.configure(build, expected)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            generated = (build / "build.ninja").read_text(encoding="utf-8")
            self.assertIn(mapped.as_posix(), generated)

    def test_generic_location_not_in_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            outside = temp / "outside/libncnn.a"
            outside.parent.mkdir(parents=True)
            outside.write_bytes(b"generic outside receipt")
            expected = self.make_root(temp / "expected")
            self.use_generic_location(expected, outside)
            self.assert_release_consumer_selects(temp, expected, outside)
            result = self.configure(temp / "build", expected)
            self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("not listed in the verified ncnn receipt", result.stdout + result.stderr)

    def test_unmapped_fallback_location_not_in_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            outside = temp / "outside/libncnn.a"
            outside.parent.mkdir(parents=True)
            outside.write_bytes(b"fallback outside receipt")
            expected = self.make_root(temp / "expected")
            self.use_unmapped_debug_fallback(expected, outside)
            self.assert_release_consumer_selects(temp, expected, outside)
            result = self.configure(temp / "build", expected)
            self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("not listed in the verified ncnn receipt", result.stdout + result.stderr)

    def test_single_config_ignores_injected_configuration_types(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            outside = temp / "outside/libncnn.a"
            outside.parent.mkdir(parents=True)
            outside.write_bytes(b"release outside receipt")
            expected = self.make_root(temp / "expected", outside)
            config = expected / "lib/cmake/ncnn/ncnnConfig.cmake"
            with config.open("a", encoding="utf-8") as stream:
                for target, filename in TARGETS.items():
                    stream.write(f'\nset_target_properties({target} PROPERTIES\n')
                    stream.write(f'  IMPORTED_LOCATION_DEBUG "{(expected / "lib" / filename).as_posix()}")\n')
            injected = ["-DCMAKE_CONFIGURATION_TYPES=Debug"]
            self.assert_release_consumer_selects(temp, expected, outside, injected)
            result = self.configure(temp / "build", expected, extra_args=injected)
            self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
            self.assertIn("not listed in the verified ncnn receipt", result.stdout + result.stderr)

            verified = self.make_root(temp / "verified")
            result = self.configure(temp / "verified-build", verified, extra_args=injected)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
