"""Offline, hash-pinned RTMO-t graph extraction for the ncnn feasibility gate.

The canonical official ONNX is the source of every weight. This does not alter
the production model pack, public API, backend, or runtime dispatch.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA256 = '20aad6e2e42359cac1c5b4a0b2da00e29bfe91a72a782fdcf287d273a04c1b24'
RAW_OUTPUTS = {
    'cls16': ('onnx::Shape_971', [1, 1, 26, 26]),
    'bbox16': ('onnx::Transpose_972', [1, 4, 26, 26]),
    'vis16': ('onnx::Transpose_973', [1, 17, 26, 26]),
    'pose16': ('onnx::Transpose_974', [1, 192, 26, 26]),
    'cls32': ('onnx::Transpose_1001', [1, 1, 13, 13]),
    'bbox32': ('onnx::Transpose_1002', [1, 4, 13, 13]),
    'vis32': ('onnx::Transpose_1003', [1, 17, 13, 13]),
    'pose32': ('onnx::Transpose_1004', [1, 192, 13, 13]),
}
DCC_INPUTS = {
    'pose': ('onnx::MatMul_1413', [1, 8, 192]),
    'boxes': ('dets', [1, 8, 5]),
    'grids': ('onnx::Sub_1429', [1, 8, 2]),
    'visibility': ('onnx::Unsqueeze_1428', [1, 8, 17]),
}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_source(path):
    if sha256(path) != SOURCE_SHA256:
        raise ValueError('official RTMO-t source SHA-256 mismatch')


def extract(model, inputs, outputs):
    import onnx
    from onnx import helper, TensorProto
    # Explicit boundary shapes make ONNX extraction independent of symbolic NMS.
    for old, shape in list(inputs.values()) + list(outputs.values()):
        model.graph.value_info.append(helper.make_tensor_value_info(old, TensorProto.FLOAT, shape))
    model = onnx.utils.Extractor(model).extract_model(
        [v[0] for v in inputs.values()], [v[0] for v in outputs.values()])
    names = {v[0]: k for k, v in {**inputs, **outputs}.items()}
    for node in model.graph.node:
        for field in (node.input, node.output):
            for i, name in enumerate(field):
                field[i] = names.get(name, name)
    for field, contract in ((model.graph.input, inputs), (model.graph.output, outputs)):
        del field[:]
        field.extend(helper.make_tensor_value_info(name, TensorProto.FLOAT, shape)
                     for name, (_, shape) in contract.items())
    del model.graph.value_info[:]
    onnx.checker.check_model(model)
    return model


def export(source: Path, output: Path):
    import onnx
    import onnxruntime as ort
    verify_source(source)
    output.mkdir(parents=True, exist_ok=True)
    specs = {
        'raw': ({'input': ('input', [1, 3, 416, 416])}, RAW_OUTPUTS),
        'dcc8': (DCC_INPUTS, {'keypoints': ('keypoints', [1, 8, 17, 3])}),
    }
    artifacts = {}
    for name, (inputs, outputs) in specs.items():
        model = extract(onnx.load(source), inputs, outputs)
        original = output / f'{name}.onnx'
        onnx.save(model, original)
        simplified = output / f'{name}.static.onnx'
        options = ort.SessionOptions()
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_BASIC
        options.optimized_model_filepath = str(simplified)
        ort.InferenceSession(str(original), options, providers=['CPUExecutionProvider'])
        artifacts[name] = {'unfolded_sha256': sha256(original), 'static_sha256': sha256(simplified),
                           'inputs': {k: v[1] for k, v in inputs.items()},
                           'outputs': {k: v[1] for k, v in outputs.items()},
                           'operators': sorted({n.op_type for n in onnx.load(simplified).graph.node})}
    contract = {'source_sha256': SOURCE_SHA256, 'artifacts': artifacts,
                'input': {'layout': 'NCHW', 'color_order': 'bgr', 'range': [0, 255],
                          'mean': [0, 0, 0], 'norm': [1, 1, 1], 'size': [416, 416],
                          'resize': 'centered affine letterbox; bilinear; pad 114'},
                'raw': {'strides': [16, 32], 'anchors': 845, 'cls': 'logits',
                        'bbox': 'center offset and log width/height', 'vis': 'logits',
                        'pose': 'learned DCC features, never keypoint coordinates'},
                'dcc': {'fixed_capacity': 8, 'learned_head_requires_gpu': True,
                        'one_forward_claim': False},
                'skeleton': {'measured': 'COCO17', 'hand_handtip_thumb': 'unavailable'}}
    (output / 'contract.json').write_text(json.dumps(contract, indent=2) + '\n')
    return contract


def repair(model):
    """One bounded standard-ONNX repair: exact Focus Conv and positive Split axes."""
    import numpy as np
    import onnx
    from onnx import helper, numpy_helper
    model = onnx.shape_inference.infer_shapes(model)
    shapes = {v.name: len(v.type.tensor_type.shape.dim)
              for v in list(model.graph.input) + list(model.graph.value_info)}
    for node in model.graph.node:
        if node.op_type == 'Split':
            for attr in node.attribute:
                if attr.name == 'axis' and attr.i < 0:
                    attr.i += shapes[node.input[0]]
    focus_names = {'Slice_16', 'Slice_18', 'Slice_20', 'Slice_21', 'Slice_22', 'Slice_23', 'Concat_24'}
    present = {n.name for n in model.graph.node} & focus_names
    if present:
        if present != focus_names:
            raise ValueError('unexpected pinned Focus graph')
        weight = np.zeros((12, 3, 2, 2), np.float32)
        for group, (dy, dx) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1))):
            for c in range(3):
                weight[group * 3 + c, c, dy, dx] = 1
        model.graph.initializer.append(numpy_helper.from_array(weight, 'focus_exact_weight'))
        nodes = [helper.make_node('Conv', ['input', 'focus_exact_weight'], ['input.1'],
                                 name='FocusExact', kernel_shape=[2, 2], strides=[2, 2])]
        nodes += [n for n in model.graph.node if n.name not in focus_names]
        del model.graph.node[:]
        model.graph.node.extend(nodes)
    onnx.checker.check_model(model)
    return model


def specialize_dcc_shapes(model):
    """Explicit follow-up ruling: only seven fixed-eight Unsqueeze shape ops."""
    import numpy as np
    import onnx
    from onnx import helper, numpy_helper
    expected = {'Unsqueeze_727', 'Unsqueeze_719', 'Unsqueeze_683', 'Unsqueeze_725',
                'Unsqueeze_714', 'Unsqueeze_680', 'Unsqueeze_724'}
    actual = {n.name for n in model.graph.node if n.op_type == 'Unsqueeze'}
    if actual != expected:
        raise ValueError('DCC static shape specialization requires exactly seven pinned Unsqueeze nodes')
    model = onnx.shape_inference.infer_shapes(model)
    shapes = {v.name: [d.dim_value for d in v.type.tensor_type.shape.dim]
              for v in model.graph.value_info}
    for node in model.graph.node:
        if node.name not in expected:
            continue
        shape = shapes[node.output[0]]
        if len(shape) != 4 or shape[:2] != [1, 8] or any(d <= 0 for d in shape):
            raise ValueError('DCC Unsqueeze output must be a complete fixed-eight static shape')
        shape_name = node.name + '_static_shape'
        model.graph.initializer.append(numpy_helper.from_array(np.array(shape, np.int64), shape_name))
        replacement = helper.make_node('Reshape', [node.input[0], shape_name], list(node.output),
                                       name=node.name + '_static_reshape')
        node.CopyFrom(replacement)
    onnx.checker.check_model(model)
    return model


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path('modelpacks/rtmo-t-416/body.onnx'))
    parser.add_argument('--output', type=Path, default=Path('out/android-rtmo/model-gate'))
    parser.add_argument('--repair', action='store_true')
    parser.add_argument('--static-dcc-shapes', action='store_true')
    args = parser.parse_args()
    if args.static_dcc_shapes:
        import onnx
        path = args.output / 'dcc8.repair.onnx'
        if sha256(path) != '79a852be117f2b24fddb52705a3106e832ef896aa792985af744987cedbb6606':
            raise ValueError('pinned repaired DCC SHA-256 mismatch')
        onnx.save(specialize_dcc_shapes(onnx.load(path)), args.output / 'dcc8.shape.onnx')
    elif args.repair:
        import onnx
        for name in ('raw', 'dcc8'):
            onnx.save(repair(onnx.load(args.output / f'{name}.static.onnx')),
                      args.output / f'{name}.repair.onnx')
    else:
        print(json.dumps(export(args.source, args.output), indent=2))
