"""Recover one pinned fused NCNN graph as static ONNX for the RKNN experiment.

This is deliberately not a general NCNN converter. The source hashes, operators
and parameters are closed; every weight byte must be consumed. Export is only a
candidate: compare real ORT outputs with the unchanged NCNN numerical/pose gates.
No training weights are downloaded or substituted, and no ModelPack is promoted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

PARAM_SHA = '908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905'
BIN_SHA = '6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9'
OPERATORS = {'Input', 'Convolution', 'Swish', 'Slice', 'Split', 'BinaryOp',
             'Concat', 'Pooling', 'Interp', 'Reshape', 'Permute'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate_source(param, weights):
    if sha(param) != PARAM_SHA or sha(weights) != BIN_SHA:
        raise ValueError('pinned NCNN source SHA mismatch; refuse unrelated graph/weights')


class WeightReader:
    """NCNN ModelBin binary layout: tagged weights, aligned halves, untagged bias."""
    def __init__(self, blob):
        self.blob, self.offset = blob, 0

    def take(self, size):
        if size < 0 or self.offset + size > len(self.blob):
            raise ValueError('truncated NCNN weight buffer')
        value = self.blob[self.offset:self.offset + size]
        self.offset += size
        return value

    def weights(self, count):
        tag = struct.unpack('<I', self.take(4))[0]
        if tag == 0x01306B47:
            size = count * 2
            value = np.frombuffer(self.take((size + 3) & ~3), dtype='<f2', count=count)
        elif tag == 0:
            value = np.frombuffer(self.take(count * 4), dtype='<f4')
        else:
            raise ValueError('unsupported NCNN weight encoding: ' + hex(tag))
        value = value.astype(np.float32)
        if not np.isfinite(value).all():
            raise ValueError('nonfinite source weights')
        return value

    def bias(self, count):
        value = np.frombuffer(self.take(count * 4), dtype='<f4').copy()
        if not np.isfinite(value).all():
            raise ValueError('nonfinite source biases')
        return value

    def finish(self):
        if self.offset != len(self.blob):
            raise ValueError('trailing NCNN weight bytes; incomplete graph recovery')


def parse_layers(text):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if len(lines) < 2 or lines[0] != '7767517':
        raise ValueError('invalid NCNN graph header')
    layer_count, blob_count = map(int, lines[1].split())
    if len(lines) - 2 != layer_count:
        raise ValueError('NCNN layer count mismatch')
    layers, seen = [], set()
    for line in lines[2:]:
        tokens = line.split()
        operator, name = tokens[:2]
        if operator not in OPERATORS:
            raise ValueError('unsupported operator: ' + operator)
        ni, no = map(int, tokens[2:4])
        inputs, outputs = tokens[4:4 + ni], tokens[4 + ni:4 + ni + no]
        if len(outputs) != no or any(value not in seen for value in inputs):
            raise ValueError('invalid graph ordering or blob count')
        if any(value in seen for value in outputs):
            raise ValueError('duplicate NCNN output blob')
        seen.update(outputs)
        parameters = {}
        for item in tokens[4 + ni + no:]:
            key, value = item.split('=')
            key = int(key)
            if key in parameters:
                raise ValueError('duplicate NCNN parameter')
            if key <= -23300:
                values = list(map(int, value.split(',')))
                if values[0] != len(values) - 1:
                    raise ValueError('invalid NCNN array length')
                parameters[key] = values[1:]
            else:
                parameters[key] = float(value) if any(c in value for c in '.eE') else int(value)
        layers.append((operator, name, inputs, outputs, parameters))
    if len(seen) != blob_count:
        raise ValueError('NCNN declared blob count mismatch')
    return layers


def recover(param, weights, output, width, height):
    validate_source(param, weights)  # Before loading ONNX or creating output files.
    if min(width, height) < 32 or max(width, height) > 960 or width % 32 or height % 32:
        raise ValueError('static width/height must be multiples of32, within32..960')
    if output.exists():
        raise ValueError('output ONNX already exists; preserve previous evidence')
    import onnx
    from onnx import helper, numpy_helper, TensorProto
    layers = parse_layers(param.read_text(encoding='utf-8'))
    reader = WeightReader(weights.read_bytes())
    nodes, initializers = [], []

    def constant(name, value, dtype=None):
        initializers.append(numpy_helper.from_array(np.array(value, dtype=dtype), name=name))
        return name

    def node(kind, inputs, outputs, name, **attrs):
        nodes.append(helper.make_node(kind, inputs, outputs, name=name, **attrs))

    for kind, name, bottom, top, p in layers:
        if kind == 'Input':
            if bottom or top != ['in0'] or p:
                raise ValueError('pinned Input contract mismatch')
        elif kind == 'Convolution':
            allowed = {0, 1, 11, 2, 12, 3, 13, 4, 14, 15, 16, 5, 6}
            if set(p) - allowed or p.get(5) != 1:
                raise ValueError('unsupported fused/quantized Convolution parameters')
            out_channels, count = int(p[0]), int(p[6])
            kw, kh = int(p[1]), int(p.get(11, p[1]))
            if count % (out_channels * kw * kh):
                raise ValueError('Convolution weight shape mismatch')
            kernel = reader.weights(count).reshape(out_channels, -1, kh, kw)
            bias = reader.bias(out_channels)
            left, upper = int(p.get(4, 0)), int(p.get(14, p.get(4, 0)))
            pads = [upper, left, int(p.get(16, upper)), int(p.get(15, left))]
            node('Conv', bottom + [constant(name + '_w', kernel), constant(name + '_b', bias)],
                 top, name, kernel_shape=[kh, kw],
                 strides=[int(p.get(13, p.get(3, 1))), int(p.get(3, 1))],
                 dilations=[int(p.get(12, p.get(2, 1))), int(p.get(2, 1))], pads=pads)
        elif kind == 'Swish':
            if p:
                raise ValueError('unexpected Swish parameters')
            node('Sigmoid', bottom, [name + '_sigmoid'], name + '_sigmoid')
            node('Mul', bottom + [name + '_sigmoid'], top, name)
        elif kind == 'Slice':
            if set(p) != {-23300, 1} or p[1] != 0 or len(p[-23300]) != len(top):
                raise ValueError('unsupported Slice contract')
            node('Split', bottom + [constant(name + '_sizes', p[-23300], np.int64)],
                 top, name, axis=1)
        elif kind == 'Split':
            if p or len(bottom) != 1:
                raise ValueError('unexpected Split alias contract')
            for index, value in enumerate(top):
                node('Identity', bottom, [value], name + '_' + str(index))
        elif kind == 'BinaryOp':
            if p != {0: 0} or len(bottom) != 2:
                raise ValueError('unsupported BinaryOp contract')
            node('Add', bottom, top, name)
        elif kind == 'Concat':
            if p != {0: 0}:
                raise ValueError('unsupported Concat axis')
            node('Concat', bottom, top, name, axis=1)
        elif kind == 'Pooling':
            if p != {0: 0, 1: 5, 11: 5, 12: 1, 13: 2, 2: 1, 3: 2, 5: 1}:
                raise ValueError('unsupported Pooling contract')
            node('MaxPool', bottom, top, name, kernel_shape=[5, 5], strides=[1, 1],
                 pads=[2, 2, 2, 2], ceil_mode=0)
        elif kind == 'Interp':
            if p != {0: 1, 1: 2.0, 2: 2.0, 6: 0}:
                raise ValueError('unsupported Interp contract')
            scales = constant(name + '_scales', [1, 1, 2, 2], np.float32)
            node('Resize', bottom + ['', scales], top, name, mode='nearest',
                 coordinate_transformation_mode='asymmetric', nearest_mode='floor')
        elif kind == 'Reshape':
            if set(p) != {0, 1} or p[0] != -1 or p[1] not in (51, 65):
                raise ValueError('unsupported Reshape contract')
            node('Reshape', bottom + [constant(name + '_shape', [1, p[1], -1], np.int64)], top, name)
        elif kind == 'Permute':
            if p != {0: 1}:
                raise ValueError('unsupported Permute contract')
            node('Transpose', bottom, top, name, perm=[0, 2, 1])
    reader.finish()
    anchors = sum((width // stride) * (height // stride) for stride in (8, 16, 32))
    graph = helper.make_graph(nodes, 'pinned_ncnn_pose_recovery',
        [helper.make_tensor_value_info('in0', TensorProto.FLOAT, [1, 3, height, width])],
        [helper.make_tensor_value_info('out0', TensorProto.FLOAT, [1, anchors, 65]),
         helper.make_tensor_value_info('out1', TensorProto.FLOAT, [1, anchors, 51])], initializers)
    model = helper.make_model(graph, producer_name='HumanVision bounded NCNN recovery',
                              opset_imports=[helper.make_opsetid('', 13)])
    model.ir_version = 9
    model = onnx.shape_inference.infer_shapes(model, strict_mode=True)
    onnx.checker.check_model(model, full_check=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(output))
    receipt = dict(source_param_sha256=PARAM_SHA, source_bin_sha256=BIN_SHA,
                   source_weight_encoding='NCNN fused FP16 storage converted exactly to FP32',
                   input_shape=[1, 3, height, width], input_color='RGB', input_normalization='divide by255 externally',
                   output_shapes=[[1, anchors, 65], [1, anchors, 51]], onnx_sha256=sha(output),
                   source_layers=len(layers), exported_nodes=len(nodes), weight_bytes_consumed=reader.offset,
                   onnx_version=onnx.__version__, numpy_version=np.__version__,
                   numerical_gate_passed=False, device_performance_verified=False, deployment_ready=False)
    output.with_suffix('.recovery.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--param', type=Path, required=True)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, default=512)
    parser.add_argument('--height', type=int, default=288)
    args = parser.parse_args()
    print(json.dumps(recover(args.param, args.bin, args.output, args.width, args.height), indent=2))


if __name__ == '__main__':
    main()
