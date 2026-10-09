"""Analyze a plain HumanVisionSettingsDemo log folder, excluding stopped/reset windows.

python tools/benchmark/analyze_settings_device_log.py SESSION_FOLDER OUTPUT_JSON --warmup 30
Uses only Python's standard library. Never copies configured video/RTSP addresses.
"""
import argparse
import csv
from datetime import datetime, timedelta
import json
import math
import re
from pathlib import Path
import statistics


def distribution(values):
    values = sorted(float(v) for v in values if math.isfinite(float(v)) and float(v) >= 0)
    if not values:
        return None
    def percentile(p):
        index = (len(values) - 1) * p
        low = int(index)
        return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (index - low)
    return {"count": len(values), "min": values[0], "p05": percentile(.05),
            "p50": percentile(.5), "p95": percentile(.95), "max": values[-1], "mean": statistics.mean(values)}


def json_lines(folder, prefix, warnings=None):
    records = []
    paths = list(folder.glob(prefix + '-*.jsonl')) + list(folder.glob(prefix + '-*.jsonl.txt'))
    for path in paths:
        content = path.read_text(encoding='utf-8-sig')
        lines = content.splitlines()
        for index, line in enumerate(lines):
            if line.strip():
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    # Force-stop can leave one buffered trailing fragment. Report
                    # that loss; never silently ignore corrupt complete/interior rows.
                    if index == len(lines) - 1 and not content.endswith('\n'):
                        if warnings is not None:
                            warnings.append(f'{path.name}: ignored incomplete final JSON row after interruption')
                        continue
                    raise
    return records


def utc(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00'))


def group_active_rows(rows, timings):
    """Keep live quality switches separate even when the input sequence continues."""
    by_utc = {(t['utc'], t['sourceId'], t['generation']): t for t in timings}
    groups, previous, active = [], None, None
    for row in rows:
        if row['sdk_state'] != 'Running' or row['source_state'] != 'Streaming':
            previous, active = None, None
            continue
        t = by_utc.get((row['utc'], row['source_id'], row['generation']), {})
        row['runtime_profile'], row['model_pack'] = t.get('runtimeProfile', ''), t.get('modelPack', '')
        fields = ('source_id', 'generation', 'source_mode', 'runtime_profile', 'model_pack')
        key = tuple(row[k] for k in fields)
        if previous is None or key != tuple(previous[k] for k in fields) or float(row['processed']) < float(previous['processed']) or float(row['elapsed_s']) - float(previous['elapsed_s']) > 3:
            active = []; groups.append(active)
        active.append(row); previous = row
    return groups


def parse_input_timing(line):
    """Use only explicitly local timestamps; packet PTS never supplies wall latency."""
    if 'gpu_color_completed sequence=' not in line:
        return None
    fields = {k: int(v) for k, v in re.findall(r'\b(sequence|generation|received_us|decoded_us|submitted_us|converted_us)=(-?\d+)', line)}
    def interval(a, b):
        first, last = fields.get(a, -1), fields.get(b, -1)
        return (last - first) / 1000 if 0 <= first <= last else -1
    return {'generation': str(fields.get('generation', -1)), 'sequence': fields.get('sequence', -1),
            'arrivalToImageObservationMs': interval('received_us', 'decoded_us'),
            'decodeToSubmitMs': interval('decoded_us', 'submitted_us'),
            'conversionFencePollMs': interval('submitted_us', 'converted_us')}


def analyze(folder, warmup):
    metadata_path = folder / 'session.json'
    if not metadata_path.exists():
        metadata_path = folder / 'session.json.txt'  # MediaStore text/plain may append .txt; content remains JSON.
    metadata = json.loads(metadata_path.read_text(encoding='utf-8-sig'))
    rows = []
    for path in folder.glob('performance-*.csv'):
        with path.open(encoding='utf-8-sig', newline='') as stream:
            rows.extend(csv.DictReader(stream))
    rows.sort(key=lambda r: float(r['elapsed_s']))
    warnings = []
    timings = json_lines(folder, 'timings', warnings)
    hardware = json_lines(folder, 'hardware', warnings)
    groups = group_active_rows(rows, timings)
    input_samples = []
    for path in sorted(folder.glob('native-*.log*')):
        for line in path.open(encoding='utf-8-sig'):
            sample = parse_input_timing(line)
            if sample and re.match(r'^\d{4}-\d\d-\d\dT', line):
                sample['utc'] = utc(line.split(' ', 1)[0]); input_samples.append(sample)
    output = []
    for active in groups:
        start, end = active[0], active[-1]
        cutoff = float(start['elapsed_s']) + warmup
        warmed = [r for r in active if float(r['elapsed_s']) >= cutoff]
        if len(warmed) < 2:
            continue
        first, last = warmed[0], warmed[-1]
        duration = float(last['elapsed_s']) - float(first['elapsed_s'])
        start_utc, end_utc = utc(first['utc']), utc(last['utc'])
        body_windows = [r for r in warmed if int(r['sdk_bodies']) > 0]
        rates = [float(r['fresh_body_results_fps']) for r in body_windows if 'fresh_body_results_fps' in r]
        result = {'source': {k: start[k] for k in ('source_id', 'generation', 'source_mode', 'width', 'height')},
                  'warmupSeconds': warmup, 'warmedSeconds': duration, 'samples': len(warmed), 'windowsWithBodies': len(body_windows)}
        for name, field in [('publicationFPS','published_frame'), ('completedFPS','processed'), ('newResultFPS','sdk_result_events'), ('newBodyResultFPS','fresh_body_result_events')]:
            if field in first:
                result[name] = (float(last[field]) - float(first[field])) / duration
        for name, field in [('resultWindowFPS','fresh_sdk_events_fps'), ('totalMs','total_ms'), ('backendMs','pose_ms'), ('localResultAgeMs','result_age_ms'), ('unityFPS','unity_frames_fps')]:
            result[name] = distribution(float(r[field]) for r in warmed)
        result['bodyWindowFPS'] = distribution(rates)
        result['bodyWindowsAtLeast20Percent'] = sum(r >= 20 for r in rates) * 100 / len(rates) if rates else None
        result['bodyPresenceWindowsPercent'] = len(body_windows) * 100 / len(warmed)
        result['pipelineErrors'] = sorted({r[k] for r in warmed for k in ('source_error','adapter_error','bridge_error','manager_error','sdk_error') if r[k]})
        timed = [t for t in timings if start_utc <= utc(t['utc']) <= end_utc and t['sourceId'] == start['source_id'] and t['generation'] == start['generation']]
        native = {}
        for t in timed:
            n = t.get('native')
            if n and n.get('available') and start_utc <= utc(n['utc']) <= end_utc:
                native[(n['frameId'], n['utc'])] = n
        result['profile'] = start['runtime_profile'] or None
        result['modelPack'] = start['model_pack'] or None
        fields = ('importPreprocessRecordMs','preprocessSubmitWaitMs','extractDownloadMs','inferenceSubmitWaitMs','denseOutputCopyMs','ownershipReleaseMs','backendSumMs')
        result['sparseNativeSamples'] = len(native)
        result['sparseStagesMs'] = {k: distribution(n[k] for n in native.values()) for k in fields}
        result['unityRenderFrameMs'] = {k: distribution(t[k] for t in timed) for k in ('unityCpuFrameMs','unityGpuFrameMs')}
        result['sdkStagesMs'] = {k: distribution(t[k] for t in timed) for k in ('sdkDetectMs','sdkPoseMs','sdkTrackingMs')}
        result['diagnosticFlushMs'] = distribution(t['diagnosticFlushMs'] for t in timed if 'diagnosticFlushMs' in t)
        measured_input = [s for s in input_samples if start_utc <= s['utc'] <= end_utc]
        result['sparseInputTimingsMs'] = {k: distribution(s[k] for s in measured_input) for k in
                                        ('arrivalToImageObservationMs', 'decodeToSubmitMs', 'conversionFencePollMs')}
        result['inputTimingScope'] = 'Arrival is the latest demux timestamp observed at image acquisition, not a proved same-packet decode duration. Decode-to-submit is local scheduling; conversion completion includes render-thread fence polling. No sensor/network latency or pure GPU duration.'
        sampled = [h for h in hardware if start_utc <= utc(h['utc']) <= end_utc]
        fields = ('appCpuPercent','appCpuOneCorePercent','systemCpuPercent','gpuPercent','gpuFrequencyMHz','processPssMB','systemAvailableMB','batteryTemperatureC','cpuTemperatureC','gpuTemperatureC')
        result['hardware'] = {k: distribution(h.get(k, -1) for h in sampled) for k in fields}
        result['hardwareStatuses'] = {k: sorted({h.get(k,'') for h in sampled}) for k in ('cpuStatus','systemCpuStatus','gpuStatus','gpuFrequencyStatus','npuStatus','memoryStatus','thermalStatus','thermalSensorsStatus','samplerStatus')}
        output.append(result)
    return {'session': folder.name, 'environment': {k: metadata.get(k) for k in ('device','gpu','unity','sdk','inputPackage','graphicsApi','deviceBuildInfo')}, 'groups': output,
            'dataQualityWarnings': warnings,
            'limits': ['New body result means one fresh notification containing at least one body, not complete 32 joints/per-person FPS.',
                       'Native six stages are serial sparse wall-time intervals, including GPU waits, not per-layer GPU time.',
                       'SDK total/current result and sparse native frames can differ: do not subtract them to fabricate CPU overhead.',
                       'Result age starts at local publication; network/decode/sensor capture are not individually timed.',
                       'GPU vendor usage is device-wide; Unity GPU frame duration is render duration, not utilization.',
                       'Negative/missing telemetry is unavailable, not zero load.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--warmup', type=float, default=30)
    args = parser.parse_args()
    result = analyze(args.folder, args.warmup)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for group in result['groups']:
        print(group['profile'], 'completed FPS', round(group['completedFPS'],3),
              'body windows >=20%', group['bodyWindowsAtLeast20Percent'], 'native samples', group['sparseNativeSamples'])
