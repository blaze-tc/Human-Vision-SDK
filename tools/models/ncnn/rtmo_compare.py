"""Compare pulled fixed-fixture ncnn outputs; never equate runner exit0 to parity."""
from pathlib import Path
import argparse
import hashlib
import json
import numpy as np


def compare(directory):
    items = [('raw', directory / 'android-raw-corrected/one', name)
             for name in ('cls16','bbox16','vis16','pose16','cls32','bbox32','vis32','pose32')]
    items.append(('dcc', directory / 'android-dcc-shape-corrected/one', 'keypoints'))
    report = {'status': 'NOT_VALIDATED', 'results': {}}
    failed = False
    for stage, folder, name in items:
        actual_file = folder / f'ncnn-gpu-{name}.fp32'
        expected_file = directory / 'one' / f'ort-{name}.fp32'
        actual = np.fromfile(actual_file, np.float32)
        expected = np.fromfile(expected_file, np.float32)
        element_count_match = actual.size == expected.size
        finite = np.isfinite(actual)
        item = {'count': len(actual), 'element_count_matches': element_count_match,
                'nonfinite_count': int((~finite).sum()),
                'actual_sha256': hashlib.sha256(actual_file.read_bytes()).hexdigest(),
                'reference_sha256': hashlib.sha256(expected_file.read_bytes()).hexdigest(),
                'max_abs': None, 'mean_abs': None}
        if element_count_match and finite.all():
            errors = abs(actual-expected)
            item.update(max_abs=float(errors.max()), mean_abs=float(errors.mean()))
            if name == 'keypoints':
                a, e = actual.reshape(8,17,3), expected.reshape(8,17,3)
                item['valid_slot0_xy_max'] = float(abs(a[0,:,:2]-e[0,:,:2]).max())
                item['valid_slot0_xy_mean'] = float(abs(a[0,:,:2]-e[0,:,:2]).mean())
                item['valid_slot0_conf_max'] = float(abs(a[0,:,2]-e[0,:,2]).max())
        else:
            failed = True
        report['results'][stage+'_'+name] = item
    # Finiteness is a necessary condition independent of any FP16 tolerance.
    # If it passes, a separately approved numeric/semantic budget is still required.
    if failed:
        report['status'] = 'FAIL_NONFINITE_OR_ELEMENT_COUNT'
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, default=Path('out/android-rtmo/model-gate'))
    args = parser.parse_args()
    report = compare(args.directory)
    payload = json.dumps(report, indent=2, allow_nan=False)+'\n'
    (args.directory / 'android-numerical-comparison.json').write_text(payload, encoding='utf-8')
    print(payload)
    raise SystemExit(1 if report['status'].startswith('FAIL') else 2)
