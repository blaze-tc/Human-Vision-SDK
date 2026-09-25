"""Fuse only the hash-pinned Body26 first ScaleNorm square/sum/sqrt chain."""
from __future__ import annotations

import hashlib
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, numpy_helper

SOURCE_SHA256 = 'bd27e32830dd11e378576b0d389999f9ba06f5d83640d29dd04f947cafa29315'
PREFIX = '/mlp/mlp.0/'
INPUT = '/Reshape_output_0'
OUTPUT = PREFIX + 'Pow_1_output_0'


def fuse(source: Path, target: Path) -> dict:
    raw = source.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SOURCE_SHA256:
        raise ValueError(f'Source ONNX SHA-256 mismatch: {digest}')
    model = onnx.load_model_from_string(raw)
    nodes = list(model.graph.node)
    chain = nodes[130:136]
    suffixes = ['Abs', 'Constant', 'Pow', 'ReduceSum', 'Constant_1', 'Pow_1']
    expected_inputs = [[INPUT], [], [PREFIX+'Abs_output_0', PREFIX+'Constant_output_0'],
                       [PREFIX+'Pow_output_0'], [], [PREFIX+'ReduceSum_output_0', PREFIX+'Constant_1_output_0']]
    for node, suffix, inputs in zip(chain, suffixes, expected_inputs):
        op = 'Constant' if suffix == 'Constant_1' else 'Pow' if suffix == 'Pow_1' else suffix
        if (node.name != PREFIX+suffix or node.op_type != op or node.domain
                or list(node.input) != inputs or list(node.output) != [PREFIX+suffix+'_output_0']):
            raise ValueError('First ScaleNorm node contract mismatch')
    for node, value in ((chain[1], 2.), (chain[4], .5)):
        if (len(node.attribute) != 1 or node.attribute[0].name != 'value'
                or node.attribute[0].type != onnx.AttributeProto.TENSOR):
            raise ValueError('First ScaleNorm scalar contract mismatch')
        array = numpy_helper.to_array(node.attribute[0].t)
        if array.shape != () or array.dtype != np.float32 or float(array) != value:
            raise ValueError('First ScaleNorm scalar value mismatch')
    attrs = {a.name: helper.get_attribute_value(a) for a in chain[3].attribute}
    if attrs != {'axes': [2], 'keepdims': 1} or any(n.attribute for n in (chain[0],chain[2],chain[5])):
        raise ValueError('First ScaleNorm axes/attributes mismatch')
    for i, node in enumerate(chain):
        consumers = [n.name for n in nodes if node.output[0] in n.input]
        expected = [chain[j].name for j in ({0:[2],1:[2],2:[3],3:[5],4:[5]}).get(i,[])] if i != 5 else [PREFIX+'Mul']
        if consumers != expected or any(o.name == node.output[0] for o in model.graph.output):
            raise ValueError('First ScaleNorm consumer contract mismatch')
    reshape = nodes[129]
    if reshape.name != '/Reshape' or list(reshape.output) != [INPUT]:
        raise ValueError('First ScaleNorm input mismatch')
    shape_node = next(n for n in nodes if reshape.input[1] in n.output)
    shape = numpy_helper.to_array(shape_node.attribute[0].t)
    inferred = onnx.shape_inference.infer_shapes(model)
    previous = next(v for v in inferred.graph.value_info if v.name == reshape.input[0])
    dims = [d.dim_value for d in previous.type.tensor_type.shape.dim]
    if shape.tolist() != [1,26,-1] or dims[1:] != [26,8,6]:
        raise ValueError('First ScaleNorm input shape mismatch')
    fused = helper.make_node('ReduceL2',[INPUT],[OUTPUT],name=PREFIX+'ReduceL2',axes=[2],keepdims=1)
    del model.graph.node[:]
    model.graph.node.extend(nodes[:130]+[fused]+nodes[136:])
    onnx.checker.check_model(model)
    target.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, target)
    return {'source_sha256':digest,'onnx_sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
            'removed_nodes':suffixes,'input':INPUT,'output':OUTPUT,'axes':[2],'keepdims':1}


if __name__ == '__main__':
    import sys, json
    print(json.dumps(fuse(Path(sys.argv[1]),Path(sys.argv[2])),indent=2))
