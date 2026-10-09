"""Analyze a plain HumanVisionSettingsDemo log folder, excluding stopped/reset windows.

python tools/benchmark/analyze_settings_device_log.py SESSION_FOLDER OUTPUT_JSON --warmup 30
Uses only Python's standard library. Never copies configured video/RTSP addresses.
"""
import argparse
import csv
from datetime import datetime, timedelta
import json
import math
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


def json_lines(folder, prefix):
    records = []
    paths = list(folder.glob(prefix + '-*.jsonl')) + list(folder.glob(prefix + '-*.jsonl.txt'))
    for path in paths:
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            if line.strip():
                records.append(json.loads(line))
    return records


def utc(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00'))


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
    groups, previous, active = [], None, None
    for row in rows:
        if row['sdk_state'] != 'Running' or row['source_state'] != 'Streaming':
            previous, active = None, None
            continue
        key = tuple(row[k] for k in ('source_id', 'generation', 'source_mode'))
        if previous is None or key != tuple(previous[k] for k in ('source_id', 'generation', 'source_mode')) or float(row['processed']) < float(previous['processed']) or float(row['elapsed_s']) - float(previous['elapsed_s']) > 3:
            active = []; groups.append(active)
        active.append(row); previous = row
    timings = json_lines(folder, 'timings')
    hardware = json_lines(folder, 'hardware')
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
        result['pipelineErrors'] = sorted({r[k] for r in warmed for k in ('source_error','adapter_error','bridge_error','manager_error','sdk_error') if r[k]})
        timed = [t for t in timings if start_utc <= utc(t['utc']) <= end_utc and t['sourceId'] == start['source_id'] and t['generation'] == start['generation']]
        native = {}
        for t in timed:
            n = t.get('native')
            if n and n.get('available') and start_utc <= utc(n['utc']) <= end_utc:
                native[(n['frameId'], n['utc'])] = n
        result['profile'] = timed[-1]['runtimeProfile'] if timed else None
        result['modelPack'] = timed[-1]['modelPack'] if timed else None
        fields = ('importPreprocessRecordMs','preprocessSubmitWaitMs','extractDownloadMs','inferenceSubmitWaitMs','denseOutputCopyMs','ownershipReleaseMs','backendSumMs')
        result['sparseNativeSamples'] = len(native)
        result['sparseStagesMs'] = {k: distribution(n[k] for n in native.values()) for k in fields}
        result['unityRenderFrameMs'] = {k: distribution(t[k] for t in timed) for k in ('unityCpuFrameMs','unityGpuFrameMs')}
        result['sdkStagesMs'] = {k: distribution(t[k] for t in timed) for k in ('sdkDetectMs','sdkPoseMs','sdkTrackingMs')}
        sampled = [h for h in hardware if start_utc <= utc(h['utc']) <= end_utc]
        fields = ('appCpuPercent','appCpuOneCorePercent','systemCpuPercent','gpuPercent','gpuFrequencyMHz','processPssMB','systemAvailableMB','batteryTemperatureC')
        result['hardware'] = {k: distribution(h[k] for h in sampled) for k in fields}
        result['hardwareStatuses'] = {k: sorted({h.get(k,'') for h in sampled}) for k in ('cpuStatus','systemCpuStatus','gpuStatus','npuStatus','memoryStatus','thermalStatus','samplerStatus')}
        output.append(result)
    return {'session': folder.name, 'environment': {k: metadata.get(k) for k in ('device','gpu','unity','sdk','inputPackage','graphicsApi','deviceBuildInfo')}, 'groups': output,
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
