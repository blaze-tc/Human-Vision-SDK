"""Static graph closure audit for converted ncnn and intermediate ONNX graphs."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.models.ncnn.model_contract import (MAX_OUTPUT_BYTES, output_blobs,
                                              validate_output_contract,
                                              verify_provenance)


DEFAULT_LAYER_ALLOWLIST = frozenset({
    "Input", "Convolution", "ConvolutionDepthWise", "Deconvolution", "BatchNorm",
    "ReLU", "PReLU", "Swish", "Sigmoid", "HardSwish", "HardSigmoid", "UnaryOp",
    "BinaryOp", "Eltwise", "Concat", "Split", "Permute", "Reshape", "Flatten",
    "InnerProduct", "Pooling", "Interp", "Crop", "Slice", "Softmax", "Reduction",
    "Padding", "MemoryData", "ShuffleChannel", "DepthToSpace", "ExpandDims",
    "Squeeze", "Gemm", "MatMul", "Normalize", "Scale", "AbsVal", "Clip",
    "Power", "Reorg", "Mish",
})
ONNX_OPERATOR_ALLOWLIST = frozenset({
    "Identity", "Constant", "Conv", "BatchNormalization", "Relu", "LeakyRelu",
    "PRelu", "Sigmoid", "HardSigmoid", "Clip", "Add", "Sub", "Mul", "Div",
    "Pow", "Exp", "Sqrt", "Abs", "Neg", "Max", "Min", "Where", "Equal",
    "Greater", "Less", "Concat", "Split", "Transpose", "Reshape", "Flatten",
    "Squeeze", "Unsqueeze", "Gather", "Slice", "Pad", "Resize", "Shape",
    "MatMul", "Gemm", "Softmax", "ReduceMean", "ReduceSum", "GlobalAveragePool",
    "MaxPool", "AveragePool",
})


def _named(name: str) -> bool:
    return bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name))


def audit_param(text: str, expected_outputs: tuple[str, ...],
                expected_input_shape: tuple[int, int, int],
                allowed_layers=DEFAULT_LAYER_ALLOWLIST) -> dict:
    if (len(expected_input_shape) != 3 or
            any(not isinstance(n, int) or n <= 0 for n in expected_input_shape)):
        raise ValueError("Expected fixed positive ncnn input shape (width, height, channels)")
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    if len(lines) < 3 or lines[0] != "7767517":
        raise ValueError("Invalid ncnn text param header")
    try:
        layer_count, blob_count = map(int, lines[1].split())
    except (ValueError, TypeError) as exc:
        raise ValueError("Invalid ncnn graph counts") from exc
    if len(lines) - 2 != layer_count or blob_count < 1:
        raise ValueError("ncnn graph count mismatch")
    layers, produced, consumed = [], set(), set()
    input_dependencies: dict[str, bool] = {}
    input_count = 0
    for line in lines[2:]:
        tokens = line.split()
        if len(tokens) < 4:
            raise ValueError("Malformed ncnn layer")
        kind, name = tokens[:2]
        if kind.lower() == "cast":
            raise ValueError(f"implicit cast layer: {name}")
        if kind not in allowed_layers:
            raise ValueError(f"unsupported ncnn layer: {kind}")
        try:
            in_count, out_count = map(int, tokens[2:4])
        except ValueError as exc:
            raise ValueError("Malformed ncnn layer counts") from exc
        if in_count < 0 or out_count < 1 or len(tokens) < 4 + in_count + out_count:
            raise ValueError("Malformed ncnn blob list")
        bottoms = tokens[4:4 + in_count]
        tops = tokens[4 + in_count:4 + in_count + out_count]
        params = tokens[4 + in_count + out_count:]
        # -1 is a valid scalar for BinaryOp (for example subtract 1), so only
        # inspect declared dimensions of layers whose params encode a shape.
        if kind in ("Input", "Reshape"):
            for param in params:
                key, separator, value = param.partition("=")
                if separator and key in ("0", "1", "2") and value.startswith("-"):
                    raise ValueError(f"dynamic or unresolved graph shape in {name}")
        if kind == "Input":
            input_count += 1
            if input_count != 1 or in_count != 0 or out_count != 1:
                raise ValueError("Expected a single Input layer")
            if tops != ["in0"]:
                raise ValueError("Expected input blob in0")
            dimensions = {}
            for param in params:
                if "=" not in param:
                    raise ValueError("Malformed input shape")
                key, value = param.split("=", 1)
                if key not in ("0", "1", "2") or key in dimensions:
                    raise ValueError("Unexpected input shape parameter")
                try:
                    dimensions[key] = int(value)
                except ValueError as exc:
                    raise ValueError("Noninteger input shape") from exc
            if tuple(dimensions.get(str(i), 0) for i in range(3)) != expected_input_shape:
                raise ValueError(f"ncnn input shape mismatch: {dimensions} != {expected_input_shape}")
        elif any(bottom not in produced for bottom in bottoms):
            raise ValueError(f"undefined bottom blob in {name}")
        consumed.update(bottoms)
        for blob in tops:
            if blob in produced:
                raise ValueError(f"Duplicate produced blob: {blob}")
            produced.add(blob)
            input_dependencies[blob] = kind == "Input" or any(
                input_dependencies.get(bottom, False) for bottom in bottoms)
        layers.append(kind)
    if input_count != 1:
        raise ValueError("Expected exactly one Input layer")
    if not expected_outputs or len(set(expected_outputs)) != len(expected_outputs) or any(not _named(x) for x in expected_outputs):
        raise ValueError("unnamed or duplicate expected output blob")
    if len(produced) != blob_count:
        raise ValueError(f"ncnn declared blob count {blob_count} != actual {len(produced)}")
    terminals = produced - consumed
    if terminals != set(expected_outputs):
        raise ValueError(f"ncnn output blobs mismatch: graph={sorted(terminals)} expected={sorted(expected_outputs)}")
    if any(not input_dependencies.get(blob, False) for blob in expected_outputs):
        raise ValueError("ncnn output is disconnected from image input in0")
    return {"layers": layers, "output_blobs": sorted(terminals)}


def audit_onnx(path: Path, expected_outputs: tuple[str, ...], expected_input_shape: tuple[int, ...],
               expected_output_contract: dict | None = None) -> dict:
    import onnx

    # A hash of the .onnx file alone cannot bind external tensor sidecars.
    # Reject them before ONNX loads any external bytes. Traverse attributes and
    # nested graphs as well as top-level initializers.
    model = onnx.load(str(path), load_external_data=False)

    def contains_external_tensor(message) -> bool:
        if isinstance(message, onnx.TensorProto) and (
                message.data_location == onnx.TensorProto.EXTERNAL or message.external_data):
            return True
        for field, value in message.ListFields():
            if field.type != field.TYPE_MESSAGE:
                continue
            if field.label == field.LABEL_REPEATED:
                if any(contains_external_tensor(item) for item in value):
                    return True
            elif contains_external_tensor(value):
                return True
        return False

    if contains_external_tensor(model):
        raise ValueError("ONNX external tensor data is not allowed by single-file SHA-256 contract")
    onnx.checker.check_model(model)
    if len(model.graph.input) != 1 or model.graph.input[0].type.tensor_type.elem_type != onnx.TensorProto.FLOAT:
        raise ValueError("ONNX image input dtype must be FLOAT32")
    if any(output.type.tensor_type.elem_type != onnx.TensorProto.FLOAT for output in model.graph.output):
        raise ValueError("ONNX numeric output dtype must be FLOAT32")
    try:
        inferred = onnx.shape_inference.infer_shapes(model, strict_mode=True)
    except onnx.shape_inference.InferenceError as exc:
        raise ValueError(f"ONNX shape inference failed: {exc}") from exc
    graph = model.graph
    if len(graph.input) != 1 or len(graph.output) != len(expected_outputs):
        raise ValueError("ONNX input/output count mismatch")
    if graph.input[0].name != "in0":
        raise ValueError("ONNX input must be named in0")
    if tuple(item.name for item in graph.output) != expected_outputs:
        raise ValueError("ONNX output blob names mismatch")
    if any(not _named(item.name) for item in graph.output):
        raise ValueError("unnamed ONNX output blob")
    dims = graph.input[0].type.tensor_type.shape.dim
    shape = tuple(dim.dim_value if not dim.dim_param else 0 for dim in dims)
    if shape != expected_input_shape or any(n <= 0 for n in shape):
        raise ValueError(f"dynamic or incompatible ONNX input shape: {shape}")
    observed_outputs = {}
    inferred_outputs = {item.name: item for item in inferred.graph.output}
    for output in graph.output:
        out_dims = output.type.tensor_type.shape.dim
        if any(dim.dim_param or dim.dim_value <= 0 for dim in out_dims):
            raise ValueError(f"dynamic ONNX output shape: {output.name}")
        if output.type.tensor_type.elem_type not in (onnx.TensorProto.FLOAT, onnx.TensorProto.FLOAT16):
            raise ValueError(f"unsupported ONNX output dtype: {output.name}")
        out_shape = [int(dim.dim_value) for dim in out_dims]
        inferred_dims = inferred_outputs[output.name].type.tensor_type.shape.dim
        inferred_shape = [int(dim.dim_value) if not dim.dim_param else 0 for dim in inferred_dims]
        if inferred_shape != out_shape:
            raise ValueError(f"ONNX declared output shape differs from inferred shape: {output.name}")
        elements = 1
        for dimension in out_shape:
            elements *= dimension
            if elements * 4 > MAX_OUTPUT_BYTES:
                raise ValueError(f"ONNX output byte bound exceeded: {output.name}")
        observed_outputs[output.name] = {"shape": out_shape, "download_dtype": "fp32",
                                         "max_bytes": elements * 4}
    if expected_output_contract is not None and observed_outputs != expected_output_contract:
        raise ValueError("ONNX output contract shape or max_bytes mismatch")
    if any(node.op_type == "Cast" for node in graph.node):
        raise ValueError("implicit cast in ONNX graph")
    if any(node.domain not in ("", "ai.onnx") for node in graph.node):
        raise ValueError("unsupported custom ONNX operator domain")
    unsupported = sorted({node.op_type for node in graph.node if node.op_type not in ONNX_OPERATOR_ALLOWLIST})
    if unsupported:
        raise ValueError(f"unsupported ONNX operator(s): {unsupported}")
    dependencies = {graph.input[0].name: True}
    for initializer in graph.initializer:
        if initializer.name == graph.input[0].name:
            raise ValueError("ONNX image input is shadowed by an initializer")
        dependencies[initializer.name] = False
    for node in graph.node:
        dependent = any(dependencies.get(name, False) for name in node.input if name)
        for name in node.output:
            dependencies[name] = dependent
    if any(not dependencies.get(name, False) for name in expected_outputs):
        raise ValueError("ONNX output is disconnected from image input in0")
    return {"opset": {entry.domain or "ai.onnx": entry.version for entry in model.opset_import},
            "input_shape": shape, "outputs": list(expected_outputs),
            "output_contract": observed_outputs}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--param", type=Path, required=True)
    parser.add_argument("--onnx", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--onnx2ncnn", type=Path)
    parser.add_argument("--ncnnoptimize", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    role = manifest["role"]
    contract = manifest["input_contract"]
    paths = {"checkpoint": args.checkpoint, "onnx": args.onnx,
             "param": args.param, "bin": args.bin, "fixture": args.fixture}
    paths.update({name: getattr(args, name) for name in ("onnx2ncnn", "ncnnoptimize")
                  if getattr(args, name) is not None})
    verify_provenance(manifest, paths)
    validate_output_contract(role, manifest["output_contract"])
    result = {"ncnn": audit_param(args.param.read_text(encoding="utf-8"), output_blobs(role),
                                  (contract["width"], contract["height"], 3)),
              "onnx": audit_onnx(args.onnx, output_blobs(role),
                                 (1, 3, contract["height"], contract["width"]),
                                 expected_output_contract=manifest["output_contract"])}
    actual_opset = result["onnx"]["opset"].get("ai.onnx")
    if actual_opset != manifest.get("opset"):
        raise ValueError(f"ONNX opset mismatch: {actual_opset} != {manifest.get('opset')}")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
