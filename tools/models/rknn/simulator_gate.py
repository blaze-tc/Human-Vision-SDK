"""Real ORT/NCNN/RKNN-simulator comparisons using immutable source-space gates.

The simulator proves only offline candidate parity. It does not establish NPU
availability, target-driver compatibility, RK3588 speed, thermals or hand points.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ncnn'))
from yolo_pose_gate import compare, associate_annotations, LIMITS, MODEL_HASHES


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def canonical_output(value, rows, columns):
    value = np.asarray(value)
    # Only the known batch dimension can be removed. Equal element counts do
    # not permit transposition/reshape that would hide a changed output schema.
    if value.shape == (1, rows, columns):
        value = value[0]
    if value.shape != (rows, columns):
        raise ValueError('candidate output shape mismatch: ' + str(value.shape))
    if value.dtype != np.float32 or not np.isfinite(value).all():
        raise ValueError('nonfinite or non-FP32 candidate output')
    return value


def uint8_rgb(chw):
    chw = np.asarray(chw)
    if chw.ndim != 3 or chw.shape[0] != 3 or not np.isfinite(chw).all():
        raise ValueError('invalid RGB input tensor')
    if np.any(chw < 0) or np.any(chw > 1):
        raise ValueError('RGB input outside unit range')
    pixels = np.rint(chw * np.float32(255)).astype(np.uint8)
    if np.max(np.abs(chw - pixels.astype(np.float32) / np.float32(255))) > 1e-7:
        raise ValueError('input is not on exact uint8 grid; refuse lossy preprocessing')
    return pixels.transpose(1, 2, 0).copy()


def read_bank(index_path, index_sha):
    if sha(index_path) != index_sha:
        raise ValueError('validation index SHA mismatch')
    index = json.loads(index_path.read_text(encoding='utf-8'))
    expected_source = dict(param_sha256=MODEL_HASHES['yolov8n_pose.ncnn.param'],
                           bin_sha256=MODEL_HASHES['yolov8n_pose.ncnn.bin'])
    if index.get('schema_version') != 1 or index.get('source_model') != expected_source:
        raise ValueError('validation source model mismatch')
    if index.get('input_shape') != [1, 3, 288, 512]:
        raise ValueError('validation bank is not the pinned Low tensor shape')
    base, bank = index_path.parent.resolve(), []
    for entry in index['fixtures']:
        folder = (base / entry['directory']).resolve()
        if not folder.is_relative_to(base):
            raise ValueError('fixture path escapes validation root')
        metadata_path = folder / 'fixture.json'
        if sha(metadata_path) != entry['fixture_sha256']:
            raise ValueError('fixture metadata SHA mismatch')
        metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
        g = metadata['geometry']
        if metadata['limits'] != LIMITS or (g['width'], g['height']) != (512, 288):
            raise ValueError('frozen fixture geometry/limits mismatch')
        if sha(folder / 'input.fp32') != metadata['input_sha256']:
            raise ValueError('fixture input SHA mismatch')
        tensor = np.fromfile(folder / 'input.fp32', np.float32)
        if tensor.size != 3 * 288 * 512:
            raise ValueError('fixture input count mismatch')
        tensor = tensor.reshape(3, 288, 512)
        uint8_rgb(tensor)  # Enforce the same byte preprocessing for both paths.
        reference = []
        execution = metadata['reference_execution']
        if execution.get('exit_code') != 0 or execution.get('kind') != 'actual NCNN CPU host runner':
            raise ValueError('missing completed NCNN reference execution')
        if sha(folder / 'ncnn-reference.log') != execution['log_sha256']:
            raise ValueError('NCNN reference log SHA mismatch')
        for blob, columns in [('out0', 65), ('out1', 51)]:
            path = folder / ('ncnn-cpu-' + blob + '.fp32')
            if sha(path) != execution['output_sha256'][blob]:
                raise ValueError('NCNN reference output SHA mismatch')
            value = np.fromfile(path, np.float32)
            if value.size != 3024 * columns:
                raise ValueError('NCNN reference output count mismatch')
            reference.append(canonical_output(value.reshape(3024, columns), 3024, columns))
        bank.append((entry['directory'], metadata, tensor, reference))
    if {name for name, *_ in bank} != {'seven', 'one', 'empty'} or len(bank) != 3:
        raise ValueError('validation requires all three fixed fixture controls')
    return bank


def evaluate(bank, predict, output, kind, identity):
    if output.exists():
        raise ValueError('gate output already exists; preserve prior candidate evidence')
    output.mkdir(parents=True)
    records = []
    for name, metadata, tensor, reference in bank:
        raw = predict(tensor)
        if len(raw) != 2:
            raise ValueError('candidate must return exactly the two raw pose tensors')
        actual = [canonical_output(raw[0], 3024, 65), canonical_output(raw[1], 3024, 51)]
        result = compare(*reference, *actual, metadata['geometry'], metadata['expected_people'])
        if metadata.get('annotations'):
            result['annotation_reference'] = associate_annotations(result['reference'], metadata['annotations'])
            result['annotation_actual'] = associate_annotations(result['actual'], metadata['annotations'])
            result['passed'] &= result['annotation_reference']['passed'] and result['annotation_actual']['passed']
        if name == 'seven':
            # Existing seven-person raised-arm evidence: do not accept seven
            # boxes while quietly losing the observed shoulder/wrist joints.
            arms = []
            g = metadata['geometry']
            for body in result.get('actual', []):
                shoulder, wrist = body['joints'][5], body['joints'][9]
                valid = all(j[2] >= .2 and 0 <= j[0] < g['source_width'] and
                            0 <= j[1] < g['source_height'] for j in (shoulder, wrist))
                arms.append(bool(valid and wrist[1] < shoulder[1]))
            result['raised_arm_checks'] = arms
            result['passed'] &= len(arms) == 7 and all(arms)
        hashes = {}
        for blob, value in zip(('out0', 'out1'), actual):
            path = output / (name + '-' + blob + '.fp32')
            value.tofile(path)
            hashes[blob] = sha(path)
        result.update(fixture=name, input_sha256=metadata['input_sha256'], output_sha256=hashes)
        records.append(result)
        print(kind, name, 'PASS' if result['passed'] else 'FAIL', 'raw:', result['raw'], flush=True)
    report = dict(kind=kind, identity=identity, offline_numerical_passed=all(r['passed'] for r in records),
                  deployment_ready=False, device_performance_verified=False, limits=LIMITS, fixtures=records)
    (output / 'comparison.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return report


def onnx_gate(onnx_path, index_path, index_sha, output):
    import onnxruntime as ort
    options = ort.SessionOptions()
    options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(onnx_path), sess_options=options, providers=['CPUExecutionProvider'])
    if session.get_inputs()[0].shape != [1, 3, 288, 512] or [v.name for v in session.get_outputs()] != ['out0', 'out1']:
        raise ValueError('recovered ONNX tensor contract mismatch')
    report = evaluate(read_bank(index_path, index_sha),
        lambda tensor: session.run(['out0', 'out1'], {'in0': tensor[None]}), output,
        'ORT recovered graph vs NCNN CPU', dict(onnx_sha256=sha(onnx_path),
            validation_index_sha256=index_sha, onnxruntime_version=ort.__version__, provider=session.get_providers()))
    return report


def rknn_gate(rknn, index_path, index_sha, output, model_sha):
    result = rknn.init_runtime()  # PC simulator, explicitly no target/device.
    if result != 0:
        raise RuntimeError('RKNN simulator init failed: ' + str(result))
    return evaluate(read_bank(index_path, index_sha),
        lambda tensor: rknn.inference(inputs=[uint8_rgb(tensor)[None]], data_format=['nhwc']),
        output, 'RKNN x86 simulator vs NCNN CPU', dict(rknn_sha256=model_sha,
            validation_index_sha256=index_sha, actual_execution='PC simulator; no hardware target'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--onnx', type=Path, required=True)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--index-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = onnx_gate(args.onnx, args.index, args.index_sha256, args.output)
    raise SystemExit(0 if result['offline_numerical_passed'] else 1)


if __name__ == '__main__':
    main()
