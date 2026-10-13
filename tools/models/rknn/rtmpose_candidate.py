"""Local Body26 RKNN feasibility probe; never installs or qualifies a runtime pack.

Uses the existing real-image bank with its hashes and annotations. Normalization
is embedded exactly once in RKNN. ORT receives normalized NCHW float32; RKNN
receives raw RGB NHWC uint8. Simulator time is deliberately not reported as FPS.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import sys
from pathlib import Path

import numpy as np

if __package__ in (None, ''):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tools.models.ncnn.compare_pose_outputs import compare_pose

BODY_SHA256 = '7d0075fbc44b2e696af21ac4555333fbb43405bf8dcc8d44c17c98d23818c710'
MEAN = np.array([123.675, 116.28, 103.53], np.float32)
STD = np.array([58.395, 57.12, 57.375], np.float32)
SHAPES = [(1, 26, 384), (1, 26, 512)]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_hash(path, expected):
    if sha256(path) != expected:
        raise ValueError('SHA-256 mismatch: ' + str(path))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def fixed_model(source, output):
    """Fix only tensor dimension metadata, retaining every node and initializer."""
    import onnx
    from onnxruntime.tools.make_dynamic_shape_fixed import make_input_shape_fixed
    verify_hash(source, BODY_SHA256)
    if output.exists():
        raise ValueError('Refuse to overwrite model evidence')
    model = onnx.load(str(source))
    make_input_shape_fixed(model.graph, 'input', [1, 3, 256, 192])
    for value, dims in zip(model.graph.output, SHAPES):
        for dim, size in zip(value.type.tensor_type.shape.dim, dims):
            dim.ClearField('dim_param')
            dim.dim_value = size
    model = onnx.shape_inference.infer_shapes(model)
    onnx.checker.check_model(model)
    onnx.save(model, str(output))
    return model


def decode(heads, inverse, bbox):
    """SimCC argmax/split_ratio=2, then map the crop back to source pixels."""
    if len(heads) != 2 or any(v.shape != s for v, s in zip(heads, SHAPES)):
        raise ValueError('Expected Body26 SimCC heads')
    if any(not np.isfinite(v).all() for v in heads):
        raise ValueError('Nonfinite SimCC output')
    x, y = heads
    scores = np.minimum(x.max(2), y.max(2))[0]
    points = np.stack([x.argmax(2)[0], y.argmax(2)[0]], axis=1).astype(np.float32) / 2
    points = points @ np.asarray(inverse)[:, :2].T + np.asarray(inverse)[:, 2]
    valid = ((scores > 0) & (points[:, 0] >= bbox[0]) & (points[:, 0] <= bbox[2]) &
             (points[:, 1] >= bbox[1]) & (points[:, 1] <= bbox[3]))
    return dict(points=points.tolist(), scores=scores.tolist(), valid=valid.tolist(), bbox=bbox)


def compare_heads(reference, candidate, inverse, bbox, image_size):
    """Use unchanged source-space Body26 golden limits (1% p95, 3% max, .02 score)."""
    result = compare_pose(decode(reference, inverse, bbox), decode(candidate, inverse, bbox),
                          bbox, image_size)
    result['head_max_abs'] = [float(np.max(np.abs(a - b))) for a, b in zip(reference, candidate)]
    return result


def prepare(source, bank, bank_hash, output):
    """Build reproducible crops from pinned real images, including mirror/rotation."""
    import cv2
    import onnxruntime as ort
    verify_hash(source, BODY_SHA256)
    verify_hash(bank / 'index.json', bank_hash)
    if output.exists():
        raise ValueError('Use a new output directory')
    output.mkdir(parents=True)
    fixed_model(source, output / 'body-fixed.onnx')
    options = ort.SessionOptions()
    # Shape-dependent ORT fusions can change floating point summation order.
    # Disable them for the metadata-only equivalence test; retain all model ops.
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    sessions = [ort.InferenceSession(str(p), sess_options=options, providers=['CPUExecutionProvider'])
                for p in (source, output / 'body-fixed.onnx')]
    records = []
    for entry in json.loads((bank / 'index.json').read_text())['fixtures']:
        folder = bank / entry['directory']
        verify_hash(folder / 'fixture.json', entry['fixture_sha256'])
        fixture = json.loads((folder / 'fixture.json').read_text())
        verify_hash(folder / 'source.png', fixture['source_png_sha256'])
        image = cv2.imread(str(folder / 'source.png'))
        if image is None:
            raise ValueError('Unreadable real fixture')
        height, width = image.shape[:2]
        # Empty image has no annotated person crop; it is NOT a pose detection test.
        annotations = fixture.get('annotations', [])
        if len(annotations) != fixture['expected_people']:
            raise ValueError('Fixture person/annotation count differs')
        for index, annotation in enumerate(annotations):
            box = list(map(float, annotation['bbox_xyxy']))
            variants = [('original', image, box),
                        ('mirror', cv2.flip(image, 1), [width-box[2], box[1], width-box[0], box[3]]),
                        ('rotate', cv2.rotate(image, cv2.ROTATE_90_CLOCKWISE),
                         [height-box[3], box[0], height-box[1], box[2]])]
            for variant, pixels, bbox in variants:
                center = (np.array(bbox[:2]) + bbox[2:]) / 2
                scale = (np.array(bbox[2:]) - bbox[:2]) * 1.25
                scale[0] = max(scale[0], scale[1] * 192/256)
                scale[1] = scale[0] * 256/192
                warp = np.array([[192/scale[0], 0, 96-center[0]*192/scale[0]],
                                 [0, 256/scale[1], 128-center[1]*256/scale[1]]], np.float32)
                inverse = cv2.invertAffineTransform(warp)
                crop = cv2.warpAffine(pixels, warp, (192, 256), flags=cv2.INTER_LINEAR)
                rgb = cv2.cvtColor(crop, cv2.COLOR_BGR2RGB)
                tensor = np.transpose((rgb.astype(np.float32)-MEAN)/STD, (2, 0, 1))[None].copy()
                heads = [session.run(None, {'input': tensor}) for session in sessions]
                # Metadata fixing must introduce zero numeric change, not merely pass a loose gate.
                if not all(np.array_equal(a, b) for a, b in zip(*heads)):
                    raise ValueError('Fixed shape changed ORT numerical outputs')
                metrics = compare_heads(heads[0], heads[1], inverse, bbox,
                                        (pixels.shape[1], pixels.shape[0]))
                name = f"{entry['directory']}-{index}-{variant}"
                np.savez(output / (name+'.npz'), rgb=rgb, x=heads[0][0], y=heads[0][1])
                records.append(dict(name=name, file=name+'.npz', sha256=sha256(output/(name+'.npz')),
                                    inverse=inverse.tolist(), bbox=bbox,
                                    image_size=[pixels.shape[1], pixels.shape[0]], ort=metrics))
    if not records:
        raise ValueError('Empty crop bank')
    receipt = dict(schema_version=1, source_sha256=BODY_SHA256,
                   fixed_sha256=sha256(output/'body-fixed.onnx'), bank_sha256=bank_hash,
                   normalization=dict(mean=MEAN.tolist(), std=STD.tolist(), color='RGB'),
                   cases=records, deployment_ready=False, device_performance_verified=False)
    write_json(output/'index.json', receipt)
    return receipt


def validate_features(outputs):
    """The split transfers 1,248 features, not full-resolution image data."""
    if len(outputs) != 1 or outputs[0].shape != (1, 26, 8, 6) or not np.isfinite(outputs[0]).all():
        raise ValueError('Invalid NCHW Body26 split features')
    return outputs[0]


def chain_pool5(model):
    """9/13 stride-one max pools equal two/three 5 pools, including padded edges.

    RKNN 2.3.2 golden already diverges at this source's 9x9 pool. Keep max
    selection in standard 5x5 operators; no learned weights or thresholds change.
    """
    from onnx import helper
    nodes = {n.name: n for n in model.graph.node}
    for index, kernel in enumerate([5, 9, 13]):
        node = nodes.get(f'MaxPool_{96+index}')
        expected = dict(ceil_mode=0, kernel_shape=[kernel]*2,
                        pads=[kernel//2]*4, strides=[1, 1])
        if (node is None or node.op_type != 'MaxPool' or node.domain or
                list(node.input) != ['340'] or list(node.output) != [str(341+index)] or
                {a.name: helper.get_attribute_value(a) for a in node.attribute} != expected):
            raise ValueError('Pinned SPP pool contract changed')
    for index in [1, 2]:
        node = nodes[f'MaxPool_{96+index}']
        node.CopyFrom(helper.make_node('MaxPool', [str(340+index)], [str(341+index)],
                                      name=node.name, kernel_shape=[5, 5], pads=[2]*4,
                                      strides=[1, 1], ceil_mode=0))
    return model


def split_model(source, output, pool5=False):
    """Extract exact existing subgraphs; all trained weights/operations retained."""
    import onnx
    from onnx import helper, TensorProto
    verify_hash(source, 'df995f664f467fe54bfbf82ba8f758b0eb9b9412ab078bc0510f4d16e7b8843b')
    model = onnx.load(str(source))
    if pool5:
        model = chain_pool5(model)
    model.graph.value_info.append(helper.make_tensor_value_info('378', TensorProto.FLOAT, [1, 26, 8, 6]))
    extractor = onnx.utils.Extractor(model)
    for name, inputs, outputs in [('backbone', ['input'], ['378']),
                                 ('head', ['378'], ['simcc_x', 'simcc_y'])]:
        part = extractor.extract_model(inputs, outputs)
        onnx.checker.check_model(part)
        onnx.save(part, str(output/(name+'.onnx')))


def convert(prepared, prepared_hash, output, split_head=False, external_normalization=False,
            optimization_level=3, pool5=False):
    """Compile and compare the actual x86 RKNN simulator, with raw RGB input."""
    from rknn.api import RKNN
    version = importlib.metadata.version('rknn-toolkit2')
    if version.split('+')[0] != '2.3.2':
        raise ValueError('Requires RKNN Toolkit2 2.3.2')
    verify_hash(prepared/'index.json', prepared_hash)
    bank = json.loads((prepared/'index.json').read_text())
    if bank['source_sha256'] != BODY_SHA256 or not bank['cases']:
        raise ValueError('Unexpected source or empty corpus')
    verify_hash(prepared/'body-fixed.onnx', bank['fixed_sha256'])
    for case in bank['cases']:
        verify_hash(prepared/case['file'], case['sha256'])
    if output.exists():
        raise ValueError('Use a new output directory')
    output.mkdir(parents=True)
    receipt = dict(toolkit_version=version, prepared_sha256=prepared_hash,
                   numerical_gate_passed=False, deployment_ready=False,
                   device_performance_verified=False, split_head=split_head,
                   external_normalization=external_normalization, optimization_level=optimization_level,
                   pool5=pool5, cases=[])
    rknn = RKNN(verbose=False)
    def checked(stage, status):
        receipt['stage'] = stage
        if status != 0:
            raise RuntimeError(f'{stage} failed with {status}')
    try:
        source = prepared/'body-fixed.onnx'
        head = None
        if split_head:
            import onnxruntime as ort
            split_model(source, output, pool5)
            options = ort.SessionOptions()
            options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
            original = ort.InferenceSession(str(source), options, providers=['CPUExecutionProvider'])
            backbone = ort.InferenceSession(str(output/'backbone.onnx'), options, providers=['CPUExecutionProvider'])
            head = ort.InferenceSession(str(output/'head.onnx'), options, providers=['CPUExecutionProvider'])
            # Prove the split itself changes neither points nor raw output tensors.
            for case in bank['cases']:
                with np.load(prepared/case['file'], allow_pickle=False) as data:
                    tensor = ((data['rgb'].astype(np.float32)-MEAN)/STD).transpose(2, 0, 1)[None].copy()
                    features = validate_features(backbone.run(None, {'input': tensor}))
                    heads = head.run(None, {'378': features})
                    reference = original.run(None, {'input': tensor})
                    if not all(np.array_equal(a, b) for a, b in zip(heads, reference)):
                        raise ValueError('Graph split changed ORT outputs')
                    # Windows and Linux ORT kernels can round differently. The
                    # reference bank still must meet the unchanged joint gate.
                    compare_heads([data['x'], data['y']], reference, case['inverse'],
                                  case['bbox'], tuple(case['image_size']))
            source = output/'backbone.onnx'
            receipt['split_ort_exact'] = True
            receipt['head_sha256'] = sha256(output/'head.onnx')
        receipt['compiled_onnx_sha256'] = sha256(source)
        checked('config', rknn.config(target_platform='rk3588', optimization_level=optimization_level,
                                     mean_values=[[0, 0, 0]] if external_normalization else [MEAN.tolist()],
                                     std_values=[[1, 1, 1]] if external_normalization else [STD.tolist()]))
        checked('load', rknn.load_onnx(model=str(source)))
        checked('build', rknn.build(do_quantization=False))
        checked('export', rknn.export_rknn(str(output/'body.rknn')))
        receipt['rknn_sha256'] = sha256(output/'body.rknn')
        checked('simulator', rknn.init_runtime())
        for case in bank['cases']:
            with np.load(prepared/case['file'], allow_pickle=False) as data:
                ref = [data['x'], data['y']]
                pixels = data['rgb'][None]
                if external_normalization:
                    pixels = (pixels.astype(np.float32)-MEAN)/STD
                got = rknn.inference(inputs=[pixels], data_format=['nhwc'])
                repeat = rknn.inference(inputs=[pixels], data_format=['nhwc'])
                if head is not None:
                    got = head.run(None, {'378': validate_features(got).astype(np.float32)})
                    repeat = head.run(None, {'378': validate_features(repeat).astype(np.float32)})
                np.savez(output/(case['name']+'.npz'), x=got[0], y=got[1], repeat_x=repeat[0], repeat_y=repeat[1])
                result = dict(name=case['name'], passed=False)
                try:
                    result['metrics'] = compare_heads(ref, got, case['inverse'], case['bbox'], tuple(case['image_size']))
                    compare_heads(ref, repeat, case['inverse'], case['bbox'], tuple(case['image_size']))
                    result['repeat_identical'] = all(np.array_equal(a, b) for a, b in zip(got, repeat))
                    result['passed'] = result['repeat_identical']
                except ValueError as error:
                    result['error'] = str(error)
                receipt['cases'].append(result)
                print(json.dumps(result), flush=True)
        receipt['numerical_gate_passed'] = all(c['passed'] for c in receipt['cases'])
        if not receipt['numerical_gate_passed']:
            raise ValueError('Candidate failed unchanged Body26 numerical gate')
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        write_json(output/'receipt.json', receipt)
        rknn.release()
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--bank-sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('convert')
    p.add_argument('--prepared', type=Path, required=True)
    p.add_argument('--prepared-sha256', required=True)
    p.add_argument('--split-head', action='store_true', help='NPU convolution features + FP32 ORT head')
    p.add_argument('--external-normalization', action='store_true', help='Diagnostic FP32 normalized input')
    p.add_argument('--optimization-level', type=int, choices=range(4), default=3)
    p.add_argument('--pool5', action='store_true', help='Exact SPP 9/13 to stacked 5 rewrite; requires --split-head')
    p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare':
        result = prepare(args.source, args.bank, args.bank_sha256, args.output)
    else:
        if args.pool5 and not args.split_head:
            parser.error('--pool5 currently requires --split-head for verified FP32 head')
        result = convert(args.prepared, args.prepared_sha256, args.output, args.split_head,
                         args.external_normalization, args.optimization_level, args.pool5)
    print(json.dumps({k: v for k, v in result.items() if k != 'cases'}))


if __name__ == '__main__':
    main()
