"""Verify actual GPU fault controls against clean, same-frame preceding stages."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

NAMES = {'clean', 'channel_swap', 'vertical_flip', 'truncated_copy',
         'wrong_stride', 'wrong_packing', 'wrong_scale', 'old_slot_content'}
STAGES = ('source', 'producer', 'imported_rgb')


def verify(text):
    stages = {}
    controls = []
    for line in text.splitlines():
        match = re.search(r'HV_R4_PARITY:\s*(\{"stage":.*\})', line)
        if match:
            row = json.loads(match[1])
            stages[(row['stage'], row['generation'], row['source_id'], row['slot'])] = row
        if 'gpu_boundary_control ' in line:
            row = dict(re.findall(r'(\w+)=([^ ]+)', line.split('gpu_boundary_control ',1)[1]))
            for name in ('pass','generation','source_id','slot','baseline_clean','count','mismatches','first'):
                row[name] = int(row[name])
            row['max'] = float(row['max'])
            controls.append(row)
    if len(controls) != 24:
        raise ValueError('Expected 24 actual image/import GPU boundary controls')
    for stage in STAGES:
        selected = [row for row in controls if row['stage'] == stage]
        if len(selected) != 8 or {row['name'] for row in selected} != NAMES:
            raise ValueError('Incomplete or duplicate controls for '+stage)
        for row in selected:
            # Scheduling controls over separate submissions is safe only when
            # this control's own frame has every clean preceding boundary.
            identity = (row['generation'],row['source_id'],row['slot'])
            for prior in STAGES[:STAGES.index(stage)+1]:
                baseline = stages.get((prior,)+identity)
                if not baseline or not baseline['passed'] or baseline['mismatches'] or baseline['max'] > 1:
                    raise ValueError('Missing clean same-frame actual preceding boundary: '+prior)
            count = stages[(stage,)+identity]['elements']
            if row['pass'] != 1 or row['baseline_clean'] != 1 or row['count'] != count:
                raise ValueError('GPU boundary control rejected: '+stage+'/'+row['name'])
            if not math.isfinite(row['max']) or not 0 <= row['max'] <= 255:
                raise ValueError('Boundary control returned an invalid error magnitude')
            if row['name'] == 'clean':
                if row['mismatches'] != 0 or row['first'] != 0xffffffff or not 0 <= row['max'] <= 1:
                    raise ValueError('Clean scratch resource failed')
            elif not (0 < row['mismatches'] <= count and 0 <= row['first'] < count and row['max'] > 1):
                raise ValueError('Corrupted scratch resource did not fail at '+stage)
            row['preceding_actual_stages_verified_clean'] = True
    return controls


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    raw=args.log.read_bytes()
    result={'passed':False,'log_sha256':hashlib.sha256(raw).hexdigest(),'controls':[],'error':None}
    try:
        result['controls']=verify(raw.decode('utf-8-sig'))
        result['passed']=True
    except (ValueError,KeyError) as error:
        result['error']=str(error)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    if not result['passed']:
        raise SystemExit(result['error'])


if __name__ == '__main__':
    main()
