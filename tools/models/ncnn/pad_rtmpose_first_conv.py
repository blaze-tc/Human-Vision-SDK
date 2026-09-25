"""Pin the official Body26 ONNX and pad only its first RGB convolution.

The fourth input plane is zero in the approved GPU pack4 crop. Padding this
kernel makes the first convolution consume four channels without changing the
three-channel computation.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper


SOURCE_SHA256 = "afb78fb13754e0cb4ada4d757c73674d84316f96a39bcfc10eced545ade6c59e"
WEIGHT_SHA256 = "43817628d31cdb8e52cb2f7ac0e473942adcff33a41e0ea9b722dcbeb88345b2"
CONV_NAME = "/backbone/stem/stem.0/conv/Conv"


def pad_first_conv(source: Path, target: Path) -> dict[str, str]:
    raw = source.read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != SOURCE_SHA256:
        raise ValueError(f"Source ONNX SHA-256 mismatch: {actual}")
    model = onnx.load_from_string(raw)
    graph = model.graph
    if len(graph.input) != 1 or graph.input[0].name != "in0":
        raise ValueError("Unexpected ONNX input")
    dims = [dim.dim_value for dim in graph.input[0].type.tensor_type.shape.dim]
    if dims != [1, 3, 256, 192]:
        raise ValueError(f"Unexpected ONNX input shape: {dims}")
    convs = [node for node in graph.node if node.name == CONV_NAME]
    if len(convs) != 1 or convs[0].op_type != "Conv":
        raise ValueError("Unexpected first Conv")
    conv = convs[0]
    if list(conv.input) != ["in0", "onnx::Conv_501", "onnx::Conv_502"]:
        raise ValueError("Unexpected first Conv inputs")
    weights = [tensor for tensor in graph.initializer if tensor.name == conv.input[1]]
    if len(weights) != 1 or list(weights[0].dims) != [12, 3, 3, 3]:
        raise ValueError("Unexpected first Conv weight shape")
    weight = weights[0]
    if hashlib.sha256(weight.raw_data).hexdigest() != WEIGHT_SHA256:
        raise ValueError("First Conv weight SHA-256 mismatch")
    original = numpy_helper.to_array(weight)
    if original.dtype != np.float32:
        raise ValueError("Unexpected first Conv weight type")
    padded = np.zeros((12, 4, 3, 3), dtype=np.float32)
    padded[:, :3, :, :] = original
    replacement = numpy_helper.from_array(padded, name=weight.name)
    position = next(index for index, tensor in enumerate(graph.initializer) if tensor.name == weight.name)
    graph.initializer[position].CopyFrom(replacement)
    graph.input[0].type.tensor_type.shape.dim[1].dim_value = 4
    onnx.checker.check_model(model)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(model.SerializeToString())
    return {"source_sha256": actual, "target_sha256": hashlib.sha256(target.read_bytes()).hexdigest()}


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    arguments = parser.parse_args()
    print(pad_first_conv(arguments.source, arguments.target))
