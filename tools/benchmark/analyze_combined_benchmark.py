"""Summarize one SettingsDemo automatic comparison without counting warmup/render FPS.

Usage: python tools/benchmark/analyze_combined_benchmark.py SESSION_FOLDER OUTPUT_JSON
This reports observations, not physical pose accuracy or an RK3588 qualification.
"""
import argparse
from collections import defaultdict
from datetime import timedelta
import json
from pathlib import Path
from analyze_settings_device_log import distribution, json_lines, utc


def summarize(events, persons):
    events = sorted(events, key=lambda e: utc(e['utc']))
    measurements, pending, markers = [], None, []
    for event in events:
        if event.get('category') != 'benchmark.phase':
            continue
        message = event.get('message', '')
        markers.append({'utc': event['utc'], 'message': message})
        if ' MEASURE BEGIN;' in message:
            phase = message.split(' MEASURE BEGIN;', 1)[0]
            if pending is not None:
                measurements.append({**pending, 'complete': False})
            pending = {'phase': phase, 'begin': event['utc'], 'runtimeAtBegin': message.split(';', 1)[1].strip()}
        elif ' MEASURE END seconds=' in message:
            phase = message.split(' MEASURE END seconds=', 1)[0]
            if pending is not None and pending['phase'] == phase:
                measurements.append({**pending, 'end': event['utc'], 'complete': True,
                                     'runtimeAtEnd': message.split(';', 1)[1].strip() if ';' in message else ''})
                pending = None
        elif pending is not None and (message.startswith('interactive ') or ' opening;' in message):
            measurements.append({**pending, 'complete': False})
            pending = None
    if pending is not None:
        measurements.append({**pending, 'complete': False})
    for measurement in measurements:
        measurement['people'] = []
        if not measurement['complete']:
            continue
        begin, end = utc(measurement['begin']), utc(measurement['end'])
        measurement['durationSeconds'] = (end - begin).total_seconds()
        tracks = defaultdict(list)
        for row in persons:
            if row.get('phase') != measurement['phase']:
                continue
            sample = row['person']
            finish = utc(row['utc'])
            seconds = sample['seconds']
            # Only whole windows inside this measurement. A repeated baseline
            # with the same name must not consume another occurrence's rows.
            if seconds <= 0 or finish > end or finish - timedelta(seconds=seconds) < begin:
                continue
            key = (row['sourceId'], row['generation'], sample['stableTrackId'])
            tracks[key].append(sample)
        for (source, generation, track), samples in sorted(tracks.items()):
            seconds = sum(s['seconds'] for s in samples)
            fresh = sum(s['freshObservations'] for s in samples)
            complete = sum(s['completeCoco17'] for s in samples)
            age_samples = [s for s in samples if s['freshObservations'] > 0 and s['localResultAgeMeanMs'] >= 0]
            age_count = sum(s['freshObservations'] for s in age_samples)
            measurement['people'].append({
                'sourceId': source, 'generation': generation, 'stableTrackId': track,
                'measuredSeconds': seconds, 'freshObservations': fresh, 'completeCoco17': complete,
                'freshFps': fresh / seconds, 'completeCoco17Fps': complete / seconds,
                'freshWindowFps': distribution(s['freshFps'] for s in samples),
                'completeWindowFps': distribution(s['completeCoco17Fps'] for s in samples),
                'localAgeMeanMs': sum(s['localResultAgeMeanMs'] * s['freshObservations'] for s in age_samples) / age_count if age_count else None,
                'localAgeMaximumMs': max((s['localResultAgeMaximumMs'] for s in age_samples), default=None),
            })
    errors = [{'utc': e['utc'], 'category': e['category'], 'message': e.get('message', '')}
              for e in events if e.get('category') in ('collector.error', 'unity.Exception', 'unity.Error')]
    return {'measurements': measurements, 'markers': markers, 'errors': errors,
            'limits': ['Only completed measurement intervals are summarized; warmup is excluded.',
                       'Track windows begin on first observation and retain zero windows after absence; this is not visibility ground truth.',
                       'COCO-17 completeness excludes derived/predicted points; it does not certify full 32-joint or spatial accuracy.',
                       'Age starts at local input publication, not at camera exposure.',
                       'OnePlus CPU/Vulkan measurements do not validate RK3588 NPU speed.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    warnings = []
    result = summarize(json_lines(args.folder, 'events', warnings), json_lines(args.folder, 'persons', warnings))
    result['dataQualityWarnings'] = warnings
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for phase in result['measurements']:
        print(phase['phase'], 'complete=', phase['complete'], 'tracks=', len(phase['people']))
