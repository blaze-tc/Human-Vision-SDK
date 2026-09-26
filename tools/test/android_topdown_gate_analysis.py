"""Conservative analysis of integrated Android TopDown evidence.

No missing evidence is interpreted as a zero error count. Provisional passes
still require the user's physical movement and correctness acceptance.
"""
import argparse
import bisect
import json
import math
import re
from pathlib import Path


def _finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _nonblank(value):
    return isinstance(value, str) and bool(value.strip())


def _sha256(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{64}', value) is not None


def _percentile(values, fraction):
    values = sorted(values)
    return values[min(len(values) - 1, math.ceil(len(values) * fraction) - 1)] if values else None


def analyze(data):
    errors = []
    def require(condition, name):
        if not condition:
            errors.append(name)

    require(data.get('schema_version') == 1, 'schema_version')
    require(type(data.get('pid')) is int and data['pid'] > 0, 'process_identity')
    for field in ('serial', 'device_fingerprint', 'package'):
        require(_nonblank(data.get(field)), field)
    require(data.get('same_pid_at_end') is True and data.get('device_failures') == [],
            'device_process_or_runtime_error')
    require(data.get('install_identity_verified') is True, 'installed_apk_identity')
    require(_finite(data.get('stream_ready_log_epoch')), 'stream_ready_source_frame')
    require(data.get('source_kind') == 'live_camera', 'live_camera_source')
    require(data.get('renderer_evidence_verified') is True, 'renderer_geometry_evidence')
    capacity = data.get('capacity')
    require(type(capacity) is int and capacity in (1, 2) and
            all(data.get(field) == capacity for field in
                ('apk_capacity', 'configured_people', 'runtime_max_bodies', 'region_count')),
            'capacity_binding')
    interval = data.get('interval')
    require(type(interval) is int and 2 <= interval <= 6 and interval == data.get('expected_interval'), 'fixed_interval')
    for field in ('apk_sha256', 'profile_sha256', 'native_library_sha256'):
        value = data.get(field)
        require(_sha256(value) and value == data.get('expected_' + field), field)
    models = data.get('model_sha256', {})
    expected = data.get('expected_model_sha256', {})
    require(isinstance(models, dict) and all(_sha256(models.get(k)) and
            models[k] == expected.get(k) for k in ('detector', 'body')), 'model_sha256')
    require(data.get('backend') == 'NCNN Vulkan', 'backend')
    require(data.get('copy_path') == 'AHB Vulkan', 'gpu_copy_path')
    telemetry = data.get('runtime_telemetry')
    require(isinstance(telemetry, list) and bool(telemetry) and all(
        isinstance(item, dict) and all(type(item.get(k)) is int for k in
            ('interval','capacity','configured_people','max_bodies')) and
        item['interval'] == interval and item['capacity'] == capacity and
        item['configured_people'] == capacity and item['max_bodies'] == capacity and
        item.get('backend') == 'NCNN Vulkan' and
        item.get('copy_path') == 'AHB Vulkan'
        for item in telemetry), 'runtime_interval_capacity_copy_binding')
    backend_samples = data.get('runtime_backend_samples')
    require(isinstance(backend_samples, list) and bool(backend_samples) and
            all(item == 'NCNN Vulkan' for item in backend_samples), 'runtime_backend_binding')
    require(data.get('capture_provenance') == 'SENSOR_VERIFIED' and
            data.get('sensor_capture_verified') is True, 'sensor_capture_provenance')
    require(data.get('cpu_full_frame_readbacks') == 0, 'cpu_full_frame_readbacks')
    for field in ('gpu_copy_errors', 'gpu_import_errors'):
        require(data.get(field) == 0, field)
    requested = data.get('gpu_capture_requested')
    drops = [data.get('bridge_no_free_slot_drops'), data.get('bridge_superseded_ready_drops')]
    require(_finite(requested) and requested > 0 and all(_finite(d) and d >= 0 for d in drops) and
            sum(drops) / requested <= .01, 'bridge_drop_fraction')
    require(_finite(data.get('detector_max_capture_gap_ms')) and
            data['detector_max_capture_gap_ms'] <= 200, 'detector_capture_gap')
    require(_finite(data.get('new_track_detector_age_ms')) and
            data['new_track_detector_age_ms'] <= 200, 'new_track_detector_age')
    require(_finite(data.get('warmup_seconds')) and data['warmup_seconds'] >= 5 and
            data.get('duration_seconds') == 60, 'window_duration')
    observations = data.get('observations')
    observation_shape_valid = isinstance(observations, list) and all(isinstance(o, dict) for o in observations)
    require(observation_shape_valid, 'observation_records')
    if not observation_shape_valid:
        observations = []
    ids = [o.get('id') for o in observations]
    frames = [o.get('source_frame_id') for o in observations]
    require(all(type(v) is int and v >= 0 for v in ids + frames) and
            all(b > a for values in (ids, frames) for a,b in zip(values, values[1:])),
            'monotonic_observations')
    duration = data.get('duration_seconds', 0)
    if not _finite(duration) or duration < 0:
        duration = 0
    times = [o.get('t_ms') for o in observations]
    valid_times = len(times) == len(observations) and all(_finite(t) and 0 <= t < duration * 1000 for t in times)
    require(valid_times and all(b > a for a,b in zip(times, times[1:])), 'observation_clocks')
    if valid_times and duration > 0:
        boundaries = [0] + times + [duration * 1000]
        require(all(b - a < 1000 for a,b in zip(boundaries, boundaries[1:])),
                'one_second_result_freeze')
    else:
        require(False, 'one_second_result_freeze')
    fps = len(observations) / duration if duration else 0
    require(fps >= 29.5, 'fresh_fps_60s')
    if 'reported_fresh_fps' in data:
        require(_finite(data['reported_fresh_fps']) and abs(data['reported_fresh_fps'] - fps) < .05,
                'reported_fps_counts_frames_once')
    min_rolling_fps = None
    if valid_times and duration >= 10:
        # Evaluate every 10-second sliding window at every observation boundary,
        # including the first and last possible windows.
        last_start = (duration - 10) * 1000
        starts = {0, last_start}
        for t in times:
            if t <= last_start:
                starts.add(t)
                # A frame exits [start, start+10s) just after start passes
                # its timestamp, potentially before the next frame arrives.
                after_exit = math.nextafter(t, math.inf)
                if after_exit <= last_start:
                    starts.add(after_exit)
            entry_boundary = t - 10000
            if 0 <= entry_boundary <= last_start:
                starts.add(entry_boundary)
        min_count = len(observations)
        for start in starts:
            min_count = min(min_count,
                bisect.bisect_left(times, start + 10000) - bisect.bisect_left(times, start))
        min_rolling_fps = min_count / 10
    require(min_rolling_fps is not None and min_rolling_fps >= 29, 'fresh_fps_rolling_10s')
    ages = [o.get('sensor_age_ms') for o in observations]
    valid_ages = len(ages) == len(observations) and len(ages) > 0 and all(_finite(a) and a >= 0 for a in ages)
    p50 = _percentile(ages, .5) if valid_ages else None
    p95 = _percentile(ages, .95) if valid_ages else None
    require(valid_ages and p50 <= 75 and p95 <= 100 and max(ages) <= 250, 'sensor_age')
    revision = data.get('region_revision')
    lag_limit = data.get('max_overlay_lag_frames')
    visible = data.get('visible_person_count')
    require(type(visible) is int and visible == capacity, 'annotated_visible_count')
    require(type(lag_limit) is int and lag_limit >= 0, 'overlay_lag_limit')
    require(type(revision) is int and revision >= 0 and
            all(type(o.get('region_revision')) is int and
                o['region_revision'] == revision for o in observations), 'region_revision')
    require(type(visible) is int and type(capacity) is int and all(
                type(o.get('body_count')) is int and
                0 <= o['body_count'] <= capacity and
                o['body_count'] == o.get('native_body_count') and
                o['body_count'] <= visible for o in observations), 'body_counts')
    require(any(o.get('body_count', 0) > 0 for o in observations), 'visible_person_recall')
    first_full_body_ms = next((o.get('t_ms') for o in observations
        if type(o.get('body_count')) is int and o['body_count'] >= visible), None) if type(visible) is int else None
    require(_finite(first_full_body_ms) and first_full_body_ms <= 250, 'first_person_discovery_ms')
    require(type(visible) is int and all(o.get('body_count') == visible
        for o in observations if _finite(o.get('t_ms')) and o['t_ms'] >= 250),
        'continuous_visible_body_recall')
    # The live camera path presents by result age, not the file-playback frame
    # lag policy. Every current native body must actually be drawn.
    require(all(o.get('native_body_count') == o.get('drawn_body_count') ==
                o.get('rendered_body_count')
                for o in observations), 'native_bodies_presented')
    thermal = data.get('thermal')
    thermal_samples = thermal.get('samples') if isinstance(thermal, dict) else None
    thermal_valid = (isinstance(thermal, dict) and _finite(thermal.get('duration_seconds')) and
        thermal['duration_seconds'] >= 900 and
        thermal.get('backend') == 'NCNN Vulkan' and thermal.get('interval') == interval and
        thermal.get('capacity') == capacity and thermal.get('apk_sha256') == data.get('apk_sha256') and
        type(thermal.get('pid')) is int and thermal.get('serial') == data.get('serial') and
        thermal.get('device_fingerprint') == data.get('device_fingerprint') and
        thermal.get('package') == data.get('package') and thermal.get('pid') == data.get('pid') and
        thermal.get('same_pid_at_end') is True and thermal.get('source_kind') == 'live_camera' and
        thermal.get('evidence_verified') is True and
        _sha256(thermal.get('logcat_sha256')) and
        _finite(thermal.get('log_start_epoch')) and _finite(thermal.get('log_end_epoch')) and
        _finite(data.get('stream_ready_log_epoch')) and
        thermal['log_start_epoch'] >= data['stream_ready_log_epoch'] + 65 and
        thermal['log_end_epoch'] - thermal['log_start_epoch'] >= 900 and
        isinstance(thermal_samples, list) and len(thermal_samples) >= 901)
    if thermal_valid:
        sample_times = [s.get('t_seconds') if isinstance(s, dict) else None for s in thermal_samples]
        thermal_valid = (all(_finite(t) for t in sample_times) and
            0 <= sample_times[0] <= 1 and sample_times[-1] - sample_times[0] >= 900 and
            sample_times[-1] <= thermal['duration_seconds'] + 1 and
            all(0 < b - a <= 1.5 for a, b in zip(sample_times, sample_times[1:])))
    if thermal_valid:
        thermal_valid = all(
            _finite(s.get('gpu_frequency_mhz')) and s['gpu_frequency_mhz'] > 0 and
            _finite(s.get('fresh_frames_1s')) and s['fresh_frames_1s'] > 0 and
            _finite(s.get('fresh_fps_rolling_10s')) and s['fresh_fps_rolling_10s'] >= 29 and
            (s['t_seconds'] < 60 or (_finite(s.get('fresh_fps_rolling_60s')) and
                s['fresh_fps_rolling_60s'] >= 29.5)) and
            _finite(s.get('sensor_age_p50_ms')) and 0 <= s['sensor_age_p50_ms'] <= 75 and
            _finite(s.get('sensor_age_p95_ms')) and 0 <= s['sensor_age_p95_ms'] <= 100 and
            _finite(s.get('max_sensor_age_ms')) and 0 <= s['max_sensor_age_ms'] <= 250 and
            _finite(s.get('bridge_drop_fraction')) and 0 <= s['bridge_drop_fraction'] <= .01 and
            s.get('copy_errors') == 0 and s.get('import_errors') == 0 and
            s.get('cpu_full_frame_readbacks') == 0 and
            type(s.get('interval')) is int and type(s.get('capacity')) is int and
            type(s.get('configured_people')) is int and type(s.get('max_bodies')) is int and
            s.get('interval') == interval and s.get('capacity') == capacity and
            s.get('configured_people') == capacity and s.get('max_bodies') == capacity and
            all(type(s.get(k)) is int and s[k] == capacity for k in
                ('visible_person_count','body_count','native_body_count',
                 'drawn_body_count','rendered_body_count')) and
            s.get('copy_path') == 'AHB Vulkan' and
            s.get('backend') == 'NCNN Vulkan' and s.get('capture_provenance') == 'SENSOR_VERIFIED'
            for s in thermal_samples)
    require(thermal_valid, 'thermal_segment')
    prethermal = [error for error in errors if error != 'thermal_segment']
    return {'result': 'FAIL' if errors else 'PROVISIONAL_PASS', 'failures': errors,
            'timing_eligible': not prethermal, 'prethermal_failures': prethermal,
            'metrics': {'interval': interval, 'fresh_frames': len(observations),
                        'fresh_fps_60s': fps, 'min_rolling_10s_fps': min_rolling_fps,
                        'sensor_age_p50_ms': p50, 'sensor_age_p95_ms': p95}}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(json.loads(args.evidence.read_text(encoding='utf-8-sig')))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['result'] == 'PROVISIONAL_PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
