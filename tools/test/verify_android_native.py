"""Inspect the real Android artifact against NDK API-26 link stubs; no device claim."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def verify(ndk, library):
    tools = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin"
    readelf = tools / "llvm-readelf.exe"
    nm = tools / "llvm-nm.exe"

    def run(tool, *args):
        return subprocess.check_output([str(tool), *map(str, args)], text=True, encoding="utf-8")

    header = run(readelf, "-h", "-n", "-d", library)
    assert "AArch64" in header and "ELF64" in header, "Expected ELF64 AArch64 artifact"
    note = re.search(r"\.note.android.ident.*?description data:\s*((?:[0-9a-f]{2} ){4})", header, re.S)
    assert note, "Missing Android build note"
    api = int.from_bytes(bytes.fromhex(note[1]), "little")
    assert api == 26, f"Expected Android API 26 build note, got {api}"
    dependencies = re.findall(r"Shared library: \[([^]]+)\]", header)
    for required in ("libandroid.so", "libvulkan.so", "liblog.so", "libonnxruntime.so"):
        assert required in dependencies, f"Missing required dependency: {required}"
    assert not any("ncnn" in name for name in dependencies), "ncnn must be linked statically"
    symbols = run(nm, "--defined-only", "--demangle", library)
    for required in ("ncnn::VulkanDevice::", "ncnn::VkCompute::record_import_android_hardware_buffer(", "VkImageLayout, unsigned int, unsigned int", "vkCreateInstance"):
        assert required in symbols, f"Missing linked ncnn/Vulkan symbol: {required}"

    def dynamic_symbols(path):
        result = run(readelf, "--dyn-syms", "--wide", path)
        defined, required = set(), set()
        for line in result.splitlines():
            cols = line.split()
            if len(cols) < 8 or not cols[0].rstrip(":").isdigit():
                continue
            name = cols[7].split("@")[0]
            if cols[6] == "UND" and cols[4] == "GLOBAL":
                required.add(name)
            elif cols[6] != "UND" and cols[4] in ("GLOBAL", "WEAK"):
                defined.add(name)
        return defined, required

    # Resolve direct and transitive NEEDED libraries against the actual packaged
    # dependency inputs plus API-26 system stubs, never the host's system libraries.
    directories = [ROOT / "out/live-deps/ort-android/lib", ROOT / "out/live-deps/ffmpeg-android",
                   ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26"]
    available, required = dynamic_symbols(library)
    pending, visited = list(dependencies), set()
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        visited.add(name)
        candidates = [directory / name for directory in directories if (directory / name).is_file()]
        assert candidates, f"Unresolved dynamic dependency at API 26: {name}"
        path = candidates[0]
        exports, _ = dynamic_symbols(path)
        available.update(exports)
        pending.extend(re.findall(r"Shared library: \[([^]]+)\]", run(readelf, "-d", path)))
    missing = sorted(required - available)
    assert not missing, f"HumanVision imports unavailable from API-26/dependency libraries: {missing}"
    print(f"PASS: ELF64 AArch64, Android API {api}, ncnn static/AHB acquire and Vulkan symbols")
    print("DT_NEEDED: " + ", ".join(dependencies))
    print(f"Resolved {len(required)} strong dynamic imports against packaged inputs and API-26 stubs")
    print("SHA256: " + hashlib.sha256(library.read_bytes()).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=Path("D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK"))
    parser.add_argument("--library", type=Path, default=ROOT / "build/android-live/bin/Release/libhumanvision.so")
    args = parser.parse_args()
    verify(args.ndk, args.library)
