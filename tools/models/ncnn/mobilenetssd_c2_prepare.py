"""Prepare the pinned MobileNet-SSD Caffe conversion for the C2 Vulkan trial.

Usage: python tools/models/ncnn/mobilenetssd_c2_prepare.py RAW_PARAM OUTPUT_PARAM
The matching raw .bin is kept unchanged. The final DetectionOutput is a CPU-only
ncnn layer, so the graph exposes its three small inputs to a host SSD decoder.
"""
from pathlib import Path
import sys


def prepare(source: Path, target: Path) -> None:
    lines = source.read_text(encoding="utf-8").splitlines()
    if lines[0] != "7767517":
        raise ValueError("unexpected ncnn format")
    layers, blobs = (int(v) for v in lines[1].split())
    # caffe2ncnn leaves Caffe's top-level input implicit while retaining it in
    # the header layer count. Materialize that input with the Caffe shape.
    if len(lines) != layers + 1 or any(line.startswith("Input ") for line in lines[2:]):
        raise ValueError("unexpected implicit input layout")
    terminal = lines[-1].split()
    if terminal[:7] != ["DetectionOutput", "detection_out", "3", "1",
                        "mbox_loc", "mbox_conf_flatten", "mbox_priorbox"]:
        raise ValueError("unexpected terminal decoder")
    if sum("data_splitncnn_" in line for line in lines[2:]) != 7:
        raise ValueError("unexpected input split consumers")
    lines[1] = f"{layers} {blobs + 6}"
    # The input blob was already included in the header's blob count.
    input_layers = [
        "Input data 0 1 data 0=300 1=300 2=3",
        "Split split_data 1 7 data " + " ".join(f"data_splitncnn_{i}" for i in range(7)),
    ]
    target.write_text("\n".join(lines[:2] + input_layers + lines[2:-1]) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: mobilenetssd_c2_prepare.py RAW_PARAM OUTPUT_PARAM")
    prepare(Path(sys.argv[1]), Path(sys.argv[2]))
