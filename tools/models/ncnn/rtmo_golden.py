"""Generate offline official-ONNX boundary fixtures and verify static extraction.

Official graph NMS selects all fixtures: this script does not change thresholds
or fabricate detections. ORT here is a reference evaluator, never a runtime path.
"""
from pathlib import Path
import argparse
import json
import sys
import numpy as np
import onnx
import onnxruntime as ort
import cv2
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rtmo_contract import RAW_OUTPUTS, DCC_INPUTS, verify_source, sha256


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, default=Path('out/android-rtmo/model-gate'))
    args = parser.parse_args()
    directory = args.directory
    source = Path('modelpacks/rtmo-t-416/body.onnx')
    verify_source(source)
    model = onnx.load(source)
    extra = {**RAW_OUTPUTS, **{k: v for k, v in DCC_INPUTS.items() if k != 'boxes'}}
    for original, shape in extra.values():
        # The official graph has dynamic NMS count, so do not impose capacity8 here.
        shape = [None] * len(shape)
        model.graph.output.append(onnx.helper.make_tensor_value_info(original, onnx.TensorProto.FLOAT, shape))
    model.graph.output.append(onnx.helper.make_tensor_value_info('y.7', onnx.TensorProto.INT64, [None, None]))
    original = ort.InferenceSession(model.SerializeToString(), providers=['CPUExecutionProvider'])
    raw_sessions = [ort.InferenceSession(str(directory / f'raw.{suffix}.onnx'), providers=['CPUExecutionProvider']) for suffix in ('static', 'repair')]
    dcc_sessions = [ort.InferenceSession(str(directory / f'dcc8.{suffix}.onnx'), providers=['CPUExecutionProvider']) for suffix in ('static', 'repair')]
    records = []
    fixtures = [('one', 'tests/testdata/d0_3_one_person.mp4', 1000),
                ('two', 'tests/testdata/d0_3_two_people.mp4', 1000),
                ('empty', None, 0)]
    for name, video, ms in fixtures:
        target = directory / name
        target.mkdir(exist_ok=True)
        source_hash = None
        if video:
            cap = cv2.VideoCapture(video)
            cap.set(cv2.CAP_PROP_POS_MSEC, ms)
            ok, frame = cap.read()
            cap.release()
            if not ok:
                raise ValueError(f'cannot read {video}')
            source_hash = sha256(video)
            h, w = frame.shape[:2]
            scale = min(416 / w, 416 / h)
            matrix = np.array([[scale, 0, (416-w*scale)/2], [0, scale, (416-h*scale)/2]], np.float32)
            prepared = cv2.warpAffine(frame, matrix, (416, 416), flags=cv2.INTER_LINEAR,
                                     borderMode=cv2.BORDER_CONSTANT, borderValue=(114,114,114))
        else:
            prepared = np.full((416,416,3), 114, np.uint8)
        tensor = prepared.transpose(2,0,1)[None].astype(np.float32)
        tensor.tofile(target / 'input.fp32')
        values = original.run(None, {'input': tensor})
        reference = {out.name: v for out, v in zip(original.get_outputs(), values)}
        dets = reference['dets']
        accepted = np.flatnonzero(dets[0,:,4] >= .35)[:8]
        # Padded entries are invalid; use real reference index0 only to keep DCC shapes finite.
        indices = np.zeros(8, np.int64)
        indices[:len(accepted)] = accepted
        dcc_input = {'boxes': dets[:,indices]}
        for key, (old, _) in DCC_INPUTS.items():
            if key != 'boxes':
                dcc_input[key] = reference[old][:,indices]
        for key, value in dcc_input.items():
            value.tofile(target / f'{key}.fp32')
        errors = {}
        for label, session in zip(('raw_static','raw_repair'), raw_sessions):
            for output, value in zip(session.get_outputs(), session.run(None, {'input': tensor})):
                expected = reference[RAW_OUTPUTS[output.name][0]]
                errors[f'{label}_{output.name}'] = float(np.max(np.abs(value-expected)))
                np.testing.assert_allclose(value, expected, rtol=2e-4, atol=2e-4)
                expected.tofile(target / f'ort-{output.name}.fp32')
        for label, session in zip(('dcc_static','dcc_repair'), dcc_sessions):
            value = session.run(None, dcc_input)[0]
            expected = reference['keypoints'][:,indices]
            errors[label] = float(np.max(np.abs(value-expected)))
            np.testing.assert_allclose(value, expected, rtol=2e-4, atol=0.002)
            expected.tofile(target / 'ort-keypoints.fp32')
        record = {'fixture': name, 'source': video, 'source_sha256': source_hash,
                  'source_ms': ms, 'input_sha256': sha256(target / 'input.fp32'),
                  'accepted_at_0_35': len(accepted), 'selected_official_nms_indices': reference['y.7'][0,indices].tolist(),
                  'valid_slots': len(accepted), 'max_absolute_errors': errors}
        records.append(record)
        (target / 'fixture.json').write_text(json.dumps(record, indent=2)+'\n')
    report = {'status': 'onnx_extraction_and_repair_pass', 'ncnn_status': 'not_validated', 'fixtures': records}
    (directory / 'onnx-golden.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
