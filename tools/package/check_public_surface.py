#!/usr/bin/env python3
"""Check frozen V1 signatures and reject model/backend details in public SDK source."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


IMPLEMENTATION_TOKENS = (
    "RTMO", "RTMPose", "COCO", "Halpe", "QNN", "NNAPI", "DirectML",
    "RKNN", "ncnn", "ONNX",
)
CONCRETE_MODEL_ASSET = re.compile(
    r"[\"'][^\"']+\.(?:onnx|rknn|param|bin)[\"']", re.IGNORECASE
)
PUBLIC_MODEL_CONFIG = re.compile(r"\bpublic\b[^\n;{}]*(?:ModelPath|ModelFile)", re.IGNORECASE)
NATIVE_MODEL_CONFIG = re.compile(r"\b(?:model_path|model_file)\w*", re.IGNORECASE)


@dataclass(frozen=True)
class Finding:
    path: str
    line_number: int
    token: str
    line: str


def normalized(text: str) -> str:
    return " ".join(text.split())


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def validate_managed_snapshot(repo: Path, snapshot_path: Path) -> list[str]:
    snapshot = load_json(snapshot_path)
    source_root = repo / snapshot["root"]
    errors: list[str] = []
    for relative_path, signatures in snapshot["required"].items():
        source_path = source_root / relative_path
        if not source_path.is_file():
            errors.append(f"missing managed V1 source: {source_path.relative_to(repo).as_posix()}")
            continue
        source = source_path.read_text(encoding="utf-8-sig")
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.DOTALL)
        source = normalized(source)
        for signature in signatures:
            if normalized(signature) not in source:
                errors.append(
                    f"missing managed V1 signature in {source_path.relative_to(repo).as_posix()}: "
                    f"{signature}"
                )
    return errors


def is_exact_exception(finding: Finding, exceptions: set[tuple[str, str, str]]) -> bool:
    return (finding.path, finding.token, normalized(finding.line).rstrip(",;")) in exceptions


def scan_file(repo: Path, path: Path, internal_interop: set[str],
              exceptions: set[tuple[str, str, str]]) -> list[Finding]:
    relative = path.relative_to(repo).as_posix()
    findings: list[Finding] = []
    for number, raw_line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith("/*") or line.startswith("*"):
            continue

        is_internal_interop = relative in internal_interop
        if path.suffix.lower() == ".cs" and not is_internal_interop and CONCRETE_MODEL_ASSET.search(line):
            findings.append(Finding(relative, number, "concrete-model-asset", line))
        if path.suffix.lower() == ".cs" and not is_internal_interop and PUBLIC_MODEL_CONFIG.search(line):
            finding = Finding(relative, number, "public-model-config", line)
            if not is_exact_exception(finding, exceptions):
                findings.append(finding)
        if path.suffix.lower() == ".h" and NATIVE_MODEL_CONFIG.search(line):
            finding = Finding(relative, number, "public-model-config", line)
            if not is_exact_exception(finding, exceptions):
                findings.append(finding)

        inspect_tokens = path.suffix.lower() == ".h" or "public" in line
        if is_internal_interop:
            inspect_tokens = False
        if inspect_tokens:
            for token in IMPLEMENTATION_TOKENS:
                if re.search(re.escape(token), line, re.IGNORECASE):
                    finding = Finding(relative, number, token, line)
                    if not is_exact_exception(finding, exceptions):
                        findings.append(finding)
    return findings


def run_self_test() -> None:
    exact = {("a.h", "ONNX", "HV_BACKEND_ONNX_CPU = 1")}
    allowed = Finding("a.h", 1, "ONNX", "HV_BACKEND_ONNX_CPU = 1;")
    changed_value = Finding("a.h", 1, "ONNX", "HV_BACKEND_ONNX_CPU = 0;")
    changed_path = Finding("b.h", 1, "ONNX", "HV_BACKEND_ONNX_CPU = 1;")
    assert is_exact_exception(allowed, exact)
    assert not is_exact_exception(changed_value, exact)
    assert not is_exact_exception(changed_path, exact)
    model_exact = {("a.h", "public-model-config", "const char* model_path_utf8")}
    assert is_exact_exception(
        Finding("a.h", 1, "public-model-config", "const char* model_path_utf8;"),
        model_exact,
    )
    assert not is_exact_exception(
        Finding("a.h", 1, "public-model-config", "const char* other_model_path_utf8;"),
        model_exact,
    )
    assert CONCRETE_MODEL_ASSET.search('string model = "pose.onnx";')
    assert PUBLIC_MODEL_CONFIG.search("public string DetectorModelPath;")
    print("public-surface checker self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0

    repo = Path(__file__).resolve().parents[2]
    contracts = repo / "tests" / "contracts"
    snapshot_errors = validate_managed_snapshot(
        repo, contracts / "managed_v1_public_surface.json"
    )
    policy = load_json(contracts / "public_surface_legacy_exceptions.json")
    exceptions = {
        (item["path"], item["token"], normalized(item["line"]).rstrip(",;"))
        for item in policy["exceptions"]
    }
    internal_interop = set(policy["internal_interop_files"])

    paths = sorted((repo / "native" / "include" / "humanvision").glob("*.h"))
    paths += sorted((repo / "upm" / "com.blazetc.humanvision" / "Runtime").rglob("*.cs"))
    findings: list[Finding] = []
    for path in paths:
        findings.extend(scan_file(repo, path, internal_interop, exceptions))

    if snapshot_errors:
        print("Managed V1 compatibility snapshot violations:", file=sys.stderr)
        for error in snapshot_errors:
            print(f"  - {error}", file=sys.stderr)
    if findings:
        print("Public implementation-detail leaks:", file=sys.stderr)
        for finding in findings:
            print(
                f"  - {finding.path}:{finding.line_number}: "
                f"{finding.token}: {finding.line}", file=sys.stderr
            )
    if snapshot_errors or findings:
        return 1

    print("Public surface contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
