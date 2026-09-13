"""Run ONNX Runtime's mobile usability checker for shipped Android models."""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODELS = (
    ("rtmo-body", ROOT / "modelpacks/rtmo-t-416/body.onnx", "RTMO body"),
    ("rtmdet-detector", ROOT / "modelpacks/precision-t-26/detector.onnx", "RTMDet detector"),
    ("rtmpose-body26", ROOT / "modelpacks/precision-t-26/body.onnx", "RTMPose Body26"),
    ("hand21", ROOT / "modelpacks/hand21/hand.onnx", "Hand21"),
)


def run(command: list[str]) -> str:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    output = (result.stdout + "\n" + result.stderr).strip()
    if result.returncode:
        raise RuntimeError(output or f"Command failed with exit code {result.returncode}")
    return output


def clean_log(text: str) -> str:
    lines = []
    for line in text.replace("\r", "").splitlines():
        line = re.sub(r"^(?:INFO|WARNING|ERROR):\s*", "", line).rstrip()
        lines.append(line)
    return "\n".join(lines).strip()


def nnapi_section(text: str) -> str:
    cleaned = clean_log(text)
    start = cleaned.find("Checking NNAPI")
    if start < 0:
        raise RuntimeError("ORT checker output did not contain an NNAPI section")
    end = cleaned.find("================", start)
    return cleaned[start:end if end >= 0 else None].strip()


def analyze(section: str) -> dict[str, object]:
    coverage = re.findall(
        r"(\d+) partitions with a total of (\d+)/(\d+) nodes can be handled by the NNAPI EP",
        section,
    )
    if not coverage:
        raise RuntimeError("ORT checker output did not contain NNAPI node coverage")
    dynamic = re.search(r"dynamic shape=(\d+)", section)
    recommendations = re.findall(r"Model should perform well with NNAPI[^:]*:\s*(YES|NO)", section)
    rows = []
    for partitions, handled, total in coverage[:2]:
        handled_i, total_i = int(handled), int(total)
        rows.append((int(partitions), handled_i, total_i, handled_i * 100.0 / total_i))
    return {
        "as_is": rows[0],
        "fixed": rows[1] if len(rows) > 1 else None,
        "dynamic_nodes": int(dynamic.group(1)) if dynamic else 0,
        "recommendations": recommendations,
    }


def render_report(label: str, model: Path, ort_version: str, section: str, facts: dict[str, object]) -> str:
    relative = model.relative_to(ROOT).as_posix()
    digest = hashlib.sha256(model.read_bytes()).hexdigest()
    partitions, handled, total, percent = facts["as_is"]
    lines = [
        f"# {label} Android EP usability",
        "",
        f"- Model: `{relative}`",
        f"- SHA-256: `{digest}`",
        f"- Checker package: ONNX Runtime {ort_version}",
        f"- NNAPI as-is coverage: {handled}/{total} nodes ({percent:.1f}%) across {partitions} partitions",
        f"- Nodes rejected because of dynamic shape: {facts['dynamic_nodes']}",
    ]
    fixed = facts["fixed"]
    if fixed:
        f_partitions, f_handled, f_total, f_percent = fixed
        lines.append(
            f"- Checker fixed-shape estimate: {f_handled}/{f_total} nodes ({f_percent:.1f}%) across {f_partitions} partitions"
        )
    recommendations = facts["recommendations"]
    if recommendations:
        lines.append(f"- Official checker recommendation (as-is): {recommendations[0]}")
        if len(recommendations) > 1:
            lines.append(f"- Official checker recommendation (fixed-shape estimate): {recommendations[1]}")
    lines += [
        "",
        "The fixed-shape result is an analyzer estimate, not a converted model or device benchmark.",
        "Partition count and coverage do not establish latency; the forced CPU/XNNPACK/NNAPI profiles",
        "must still be measured on the same Android device and scene.",
        "",
        "## NNAPI checker excerpt",
        "",
        "```text",
        section,
        "```",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", default=sys.executable, help="Python executable containing onnxruntime.tools")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / "docs/diagnostics/android-model-usability",
    )
    args = parser.parse_args()
    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    try:
        ort_version = run([args.python, "-c", "import onnxruntime as o; print(o.__version__)"]).splitlines()[-1]
        summaries = []
        for slug, model, label in MODELS:
            if not model.is_file():
                raise RuntimeError(f"Missing model: {model}")
            output = run([
                args.python,
                "-m",
                "onnxruntime.tools.check_onnx_model_mobile_usability",
                str(model),
                "--log_level",
                "info",
            ])
            section = nnapi_section(output)
            facts = analyze(section)
            report = render_report(label, model, ort_version, section, facts)
            (output_root / f"{slug}.md").write_text(report, encoding="utf-8", newline="\n")
            summaries.append((label, f"{slug}.md", facts))
        index = [
            "# Android model usability summary",
            "",
            f"Generated with ONNX Runtime {ort_version} mobile usability checker.",
            "",
            "| Model | NNAPI coverage | Partitions | Dynamic-shape rejected nodes | Checker recommendation |",
            "| --- | ---: | ---: | ---: | --- |",
        ]
        for label, filename, facts in summaries:
            partitions, handled, total, percent = facts["as_is"]
            recommendation = facts["recommendations"][0] if facts["recommendations"] else "Unavailable"
            index.append(
                f"| [{label}]({filename}) | {handled}/{total} ({percent:.1f}%) | {partitions} | {facts['dynamic_nodes']} | {recommendation} |"
            )
        index += [
            "",
            "These are static checker results. They do not prove device execution-provider latency or throughput.",
            "",
        ]
        (output_root / "README.md").write_text("\n".join(index), encoding="utf-8", newline="\n")
    except (OSError, RuntimeError, subprocess.SubprocessError) as exception:
        print(f"Android model EP check failed: {exception}", file=sys.stderr)
        return 1
    print(f"Android model EP reports written to {output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
