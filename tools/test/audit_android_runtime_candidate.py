"""Audit an Android CPU/Vulkan/RKNN candidate without changing project inputs.

Writes three hash-bound local-evaluation audits only after every check passes.
The candidate --library replaces the SDK root; dependencies are resolved solely
from --plugin-directory and the selected NDK's Android API-26 system stubs.
This does not execute Android code or establish device/provider performance.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

try:
    from . import verify_android_native as closure
except ImportError:
    _spec = importlib.util.spec_from_file_location(
        "_hv_android_candidate_closure", Path(__file__).with_name("verify_android_native.py"))
    closure = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(closure)


ORT_SHA256 = "cd1285f8955f3abcb0127d1ffaf1e5da7893d2547872a831b4717c0bfe388328"
RKNN_SHA256 = "07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351"
AUDIT_NAMES = frozenset(("android-gpu-bridge-symbols.json",
                         "android-cpu-runtime-symbols.json",
                         "android-neural-runtime-symbols.json"))
LIMITS = {"local_evaluation_only": True, "device_performance_verified": False,
          "distribution_qualified": False, "gpu_gate": False}
RKNN_EXPORTS = frozenset(("rknn_init", "rknn_destroy", "rknn_query",
                         "rknn_set_core_mask", "rknn_inputs_set", "rknn_run",
                         "rknn_outputs_get", "rknn_outputs_release",
                         "rknn_create_mem", "rknn_destroy_mem", "rknn_set_io_mem",
                         "rknn_mem_sync"))
SDK_SYMBOLS = ("ncnn::VulkanDevice::",
               "ncnn::VkCompute::record_import_android_hardware_buffer(",
               "VkImageLayout, unsigned int, unsigned int", "vkCreateInstance",
               "AndroidVendor::Initialize(", "AndroidVendor::InputsSet(",
               "AndroidVendor::Run(", "AndroidVendor::OutputsGet(",
               "AndroidLoader::Load(", "rknn::MakePlatformLoader(")


class AuditError(ValueError):
    pass


def _require(condition, message):
    if not condition:
        raise AuditError(message)


def _hash(path):
    try:
        with Path(path).open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").hexdigest()
    except OSError as error:
        raise AuditError("Cannot hash required input: " + str(path)) from error


def verify_pinned_hash(path, expected):
    actual = _hash(path)
    _require(actual == expected.lower(), "Pinned SHA-256 mismatch: " + str(path))
    return actual


def parse_header(text):
    _require("ELF64" in text and re.search(r"Machine:\s+AArch64\b", text),
             "Expected ELF64 AArch64 artifact")
    note = re.search(r"\.note.android.ident.*?description data:\s*"
                     r"([0-9a-fA-F]{2}(?:\s+[0-9a-fA-F]{2}){3})", text, re.S)
    _require(note is not None, "Missing Android build note")
    api = int.from_bytes(bytes.fromhex(note.group(1)), "little")
    _require(api == 26, "Expected Android API 26 build note, got " + str(api))
    return {"api_level": api, "needed": _needed(text)}


def _needed(text):
    names = re.findall(r"Shared library: \[([^]]+)\]", text)
    _require(all(re.fullmatch(r"[A-Za-z0-9_.+-]+", name) for name in names),
             "Unsafe DT_NEEDED library name")
    return names


def verify_sdk_symbols(text):
    # Input is nm --defined-only --demangle, not strings/dynamic query exports.
    # OFF builds retain QueryRknnPlugin/CreateRknnSession but omit these concrete
    # Android virtual implementations and use an empty MakePlatformLoader stub.
    for symbol in SDK_SYMBOLS:
        _require(symbol in text, "Missing linked implementation symbol: " + symbol)


def verify_runtime_symbols(sdk_imports, ort_exports, rknn_exports):
    ort_api = "OrtGetApiBase@VERS_1.23.0"
    _require(ort_api in sdk_imports and ort_api in ort_exports,
             "SDK/ORT must import/export OrtGetApiBase at VERS_1.23.0 (API 23)")
    missing = RKNN_EXPORTS - rknn_exports
    _require(not missing, "Missing RKNN vendor exports: " + ", ".join(sorted(missing)))


def validate_candidate_closure(library, dependencies, packaged_directories,
                               system_directories, dynamic_symbols, needed):
    # The shared resolver searches the staged directory. It must never follow
    # a back-edge to that directory's older SDK instead of the explicit candidate.
    def checked_dependencies(path, names):
        _require("libhumanvision.so" not in names,
                 "Unsupported SDK dependency back-edge to libhumanvision.so: " + str(path))
        return names

    return closure.validate_dynamic_closure(
        library, checked_dependencies(library, dependencies), packaged_directories,
        system_directories, dynamic_symbols,
        lambda path: checked_dependencies(path, needed(path)))


def write_audits(output, documents):
    output = Path(output)
    _require(not output.exists() and not output.is_symlink(),
             "Audit output already exists; choose a new directory")
    _require(set(documents) == AUDIT_NAMES, "Expected exactly the three audit filenames")
    payloads = {}
    for name, document in documents.items():
        _require(all(document.get(key) is value for key, value in LIMITS.items()),
                 "Audit qualification flags must remain local and unverified")
        payloads[name] = json.dumps(document, indent=2, sort_keys=True,
                                    allow_nan=False) + "\n"
    # Reserve a new directory only after complete validation/serialization. No
    # project file is overwritten; x mode also rejects concurrent replacements.
    output.mkdir(exist_ok=False)
    for name, payload in payloads.items():
        with (output / name).open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)


def audit_candidate(ndk, library, plugin_directory):
    # The reused closure verifier uses assertions for its fail-closed checks.
    _require(__debug__, "Run this audit without Python -O; closure assertions are required")
    ndk, library, plugins = map(lambda p: Path(p).resolve(),
                                (ndk, library, plugin_directory))
    tools = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin"
    readelf, nm = tools / "llvm-readelf.exe", tools / "llvm-nm.exe"
    system = ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26"
    for path in (readelf, nm, library, plugins / "libonnxruntime.so", plugins / "librknnrt.so",
                 plugins / "libhumanvision_input.so"):
        _require(path.is_file(), "Required audit input missing: " + str(path))
    _require(system.is_dir(), "NDK API-26 system stubs missing")
    _require(library.name == "libhumanvision.so", "Candidate must be named libhumanvision.so")
    files = {path.name: path for path in plugins.glob("*.so")}
    files[library.name] = library  # Explicit candidate overrides the old staged SDK.
    before = {name: _hash(path) for name, path in files.items()}

    def run(tool, *arguments):
        return subprocess.check_output([str(tool), *map(str, arguments)],
                                       text=True, encoding="utf-8", timeout=60)

    metadata = parse_header(run(readelf, "-h", "-n", "-d", library))
    symbols = run(nm, "--defined-only", "--demangle", library)
    verify_sdk_symbols(symbols)
    required_dependencies = {"libandroid.so", "libvulkan.so", "liblog.so", "libonnxruntime.so"}
    _require(required_dependencies <= set(metadata["needed"]), "SDK dependency contract incomplete")
    _require(not any("ncnn" in name for name in metadata["needed"]), "ncnn must be statically linked")
    verify_pinned_hash(plugins / "libonnxruntime.so", ORT_SHA256)
    verify_pinned_hash(plugins / "librknnrt.so", RKNN_SHA256)
    dynamic_cache, needed_cache = {}, {}

    def dynamic(path):
        path = Path(path)
        if path not in dynamic_cache:
            dynamic_cache[path] = closure.parse_dynamic_symbols(run(readelf, "--dyn-syms", "--wide", path))
        return dynamic_cache[path]

    def needed(path):
        path = Path(path)
        if path not in needed_cache:
            needed_cache[path] = _needed(run(readelf, "-d", path))
        return needed_cache[path]

    _, sdk_imports = dynamic(library)
    ort_exports, _ = dynamic(plugins / "libonnxruntime.so")
    rknn_exports, _ = dynamic(plugins / "librknnrt.so")
    verify_runtime_symbols(sdk_imports, ort_exports, rknn_exports)
    counts = {}
    for name, path in sorted(files.items()):
        header = run(readelf, "-h", path)
        _require("ELF64" in header and re.search(r"Machine:\s+AArch64\b", header),
                 "Packaged dependency is not ELF64 AArch64: " + name)
        try:
            counts[name] = validate_candidate_closure(
                path, needed(path), [plugins], [system], dynamic, needed)
        except AssertionError as error:
            raise AuditError(str(error)) from error
    _require(before == {name: _hash(path) for name, path in files.items()},
             "Audit inputs changed during verification")
    common = dict(LIMITS, audit="tools/test/audit_android_runtime_candidate.py",
                  audited_utc=datetime.now(timezone.utc).isoformat(), abi="arm64-v8a",
                  api_level=26, native_sha256=before[library.name],
                  native_library=str(library), plugin_directory=str(plugins), ndk=str(ndk),
                  api26_strong_imports=counts[library.name],
                  closure_strong_imports_by_object=counts, packaged_sha256=before)
    return {
        "android-gpu-bridge-symbols.json": dict(common, ncnn_vulkan_symbols_verified=True),
        "android-cpu-runtime-symbols.json": dict(common, runtime_sha256=ORT_SHA256,
                                                ort_api_version=23, ort_cpu_symbols_verified=True),
        "android-neural-runtime-symbols.json": dict(common, runtime_sha256=RKNN_SHA256,
                                                   rknn_tensor_symbols_verified=True,
                                                   rknn_android_vendor_compiled=True,
                                                   rknn_vendor_symbols=len(RKNN_EXPORTS)),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", required=True, type=Path)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--plugin-directory", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        _require(not args.output.exists() and not args.output.is_symlink(),
                 "Audit output already exists; choose a new directory")
        documents = audit_candidate(args.ndk, args.library, args.plugin_directory)
        write_audits(args.output, documents)
    except (AuditError, OSError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1
    print("PASS: Android API26 CPU/Vulkan/RKNN static and dependency audits; no device acceptance")
    print("SDK SHA256: " + documents["android-gpu-bridge-symbols.json"]["native_sha256"])
    print("Audits: " + str(args.output.resolve()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
