"""Inspect the real Android artifact against NDK API-26 link stubs; no device claim."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def parse_dynamic_symbols(output):
    """Return loader-compatible exported names and strong imported names."""
    defined, required = set(), set()
    for line in output.splitlines():
        cols = line.split()
        if len(cols) < 8 or not cols[0].rstrip(":").isdigit():
            continue
        name = cols[7]
        binding = cols[4]
        section = cols[6]
        if section == "UND" and binding == "GLOBAL":
            # A versioned requirement must be satisfied by the same version.
            required.add(name.replace("@@", "@"))
        elif section != "UND" and binding in ("GLOBAL", "WEAK"):
            if "@@" in name:
                base, version = name.split("@@", 1)
                # A default definition satisfies both unversioned and matching
                # versioned references.
                defined.update((base, f"{base}@{version}"))
            else:
                defined.add(name)
    return defined, required


def _is_within(path, directories):
    resolved = path.resolve()
    return any(resolved.is_relative_to(directory.resolve()) for directory in directories)


def validate_dynamic_closure(root_library, dependencies, packaged_directories,
                             system_directories, dynamic_symbols, needed):
    """Validate strong imports for the root and every packaged dependency."""
    directories = list(packaged_directories) + list(system_directories)
    available, root_required = dynamic_symbols(root_library)
    imports_by_object = {root_library: root_required}
    pending, visited = list(dependencies), set()
    while pending:
        name = pending.pop()
        candidates = [directory / name for directory in directories if (directory / name).is_file()]
        assert candidates, f"Unresolved dynamic dependency at API 26: {name}"
        path = candidates[0]
        resolved = path.resolve()
        if resolved in visited:
            continue
        visited.add(resolved)
        exports, imports = dynamic_symbols(path)
        available.update(exports)
        if _is_within(path, packaged_directories):
            imports_by_object[path] = imports
        pending.extend(needed(path))

    missing_by_object = []
    for importer, required in imports_by_object.items():
        missing = sorted(required - available)
        if missing:
            missing_by_object.append(f"{importer.name}: {', '.join(missing)}")
    assert not missing_by_object, "Unresolved strong imports: " + "; ".join(missing_by_object)
    return sum(len(required) for required in imports_by_object.values())


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
        return parse_dynamic_symbols(run(readelf, "--dyn-syms", "--wide", path))

    def needed(path):
        return re.findall(r"Shared library: \[([^]]+)\]", run(readelf, "-d", path))

    # Resolve direct and transitive NEEDED libraries against the actual packaged
    # dependency inputs plus API-26 system stubs, never the host's system libraries.
    packaged = [ROOT / "out/live-deps/ort-android/lib", ROOT / "out/live-deps/ffmpeg-android"]
    system = [ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26"]
    required_count = validate_dynamic_closure(
        library, dependencies, packaged, system, dynamic_symbols, needed
    )
    print(f"PASS: ELF64 AArch64, Android API {api}, ncnn static/AHB acquire and Vulkan symbols")
    print("DT_NEEDED: " + ", ".join(dependencies))
    print(f"Resolved {required_count} strong dynamic imports from every packaged object against the closure and API-26 stubs")
    print("SHA256: " + hashlib.sha256(library.read_bytes()).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", type=Path, default=Path("D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK"))
    parser.add_argument("--library", type=Path, default=ROOT / "build/android-live/bin/Release/libhumanvision.so")
    args = parser.parse_args()
    verify(args.ndk, args.library)
