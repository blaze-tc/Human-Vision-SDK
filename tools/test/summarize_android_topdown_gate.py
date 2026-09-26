"""Turn one PID-bound device log into explicit (possibly incomplete) gate evidence."""
import argparse
import json
import re
from pathlib import Path

from tools.test.android_topdown_gate_analysis import analyze


LINE = re.compile(r'^\s*(\d+\.\d+)\s+\d+\s+\d+\s+\w+\s+\w+\s*:\s*(.*)$')
FIELD = re.compile(r'([a-z][a-z0-9_]*)=([^\s]+)')
FATAL = re.compile(r'FATAL EXCEPTION|Fatal signal|ANR in |VK_ERROR_DEVICE_LOST|'
                   r'Vulkan validation|Vulkan[^\n]*(?:device lost|fatal|failed)|'
                   r'HV_TOPDOWN_VIDEO_ERROR|\b[A-Za-z]+Exception:')


def fields(message):
    return dict(FIELD.findall(message))


def number(value, integer=False):
    try:
        return int(value) if integer else float(value)
    except (TypeError, ValueError):
        return None


def summarize(report):
    hashes = report['hashes']
    lines = report.get('log_lines', [])
    boot = None
    stats = []
    samples = []
    backend = None
    backend_lines = []
    failures = []
    camera_seen = bool(report.get('camera_source_line'))
    video_seen = bool(report.get('video_active_line'))
    for line in lines:
        match = LINE.match(line)
        if not match:
            continue
        epoch = float(match.group(1))
        message = match.group(2)
        if 'HV_TOPDOWN_BOOT ' in message:
            boot = epoch if boot is None else boot
        if 'HV_TOPDOWN_STATS ' in message:
            stats.append((epoch, fields(message)))
        if 'HV_TOPDOWN_SAMPLE ' in message:
            samples.append((epoch, fields(message)))
        if 'HV_TOPDOWN_NATIVE Actual backend=' in message:
            backend = message.partition('Actual backend=')[2].strip()
            backend_lines.append((epoch, backend))
        if 'HV_TOPDOWN_CAMERA device=' in message:
            camera_seen = True
        if 'HV_TOPDOWN_VIDEO_SOURCE_ACTIVE ' in message:
            video_seen = True
        if 'HV_TOPDOWN_NATIVE GPU worker error=' in message and not message.endswith('GPU worker error='):
            failures.append(message)
        if FATAL.search(message):
            failures.append(message)
        if 'HV_TOPDOWN_ERRORS manager=' in message:
            error_fields = fields(message)
            if error_fields.get('manager') or error_fields.get('bridge'):
                failures.append(message)
    for line in report.get('fatal_lines', []):
        if FATAL.search(line) and line not in failures:
            failures.append(line)
    if boot is None:
        failures.append('No evaluation boot marker in package PID log')
    ready = next((epoch for epoch, stat in stats
        if (number(stat.get('source_seen'), True) or 0) > 0 and
           (number(stat.get('submitted'), True) or 0) > 0), None)
    warmup_end = ready + 5 if ready is not None else float('inf')
    window_duration = min(60, max(0, report['intended_duration_seconds'] - 5))
    window_end = warmup_end + window_duration
    in_window = [item for item in stats if item[0] <= window_end]
    window_stats = [(t,s) for t,s in stats if warmup_end <= t < window_end]
    runtime_telemetry = [dict(
        interval=number(s.get('detector_interval'), True),
        capacity=number(s.get('capacity'), True),
        configured_people=number(s.get('configured_people'), True),
        max_bodies=number(s.get('max_bodies'), True),
        backend=('NCNN Vulkan' if latest == 'backend.ncnn.vulkan' else latest),
        copy_path='AHB Vulkan' if s.get('copy_path') == '1' else s.get('copy_path'))
        for t,s in window_stats
        for latest in [next((value for bt,value in reversed(backend_lines) if bt <= t), None)]]
    runtime_backend_samples = [
        'NCNN Vulkan' if value == 'backend.ncnn.vulkan' else value
        for t,value in backend_lines if warmup_end <= t < window_end]
    last = in_window[-1][1] if in_window else {}
    started = next((item for item in stats if item[0] >= warmup_end), None)
    first = started[1] if started else {}

    def delta(name):
        end = number(last.get(name), True)
        begin = number(first.get(name), True)
        return end - begin if end is not None and begin is not None and end >= begin else None

    observations = []
    for epoch, sample in samples:
        if warmup_end <= epoch < window_end:
            frame = number(sample.get('frame'), True)
            sequence = number(sample.get('sequence'), True)
            bodies = number(sample.get('bodies'), True)
            drawn = number(sample.get('drawn'), True)
            presentation = number(sample.get('presentation'), True)
            observations.append(dict(id=sequence, source_frame_id=frame,
                t_ms=(epoch - warmup_end) * 1000, body_count=bodies,
                native_body_count=bodies, facade_slot_count=drawn,
                drawn_body_count=None, rendered_body_count=None,
                region_revision=number(sample.get('revision'), True),
                unity_observed_age_ms=number(sample.get('observed_age_ms')),
                source_presentation_lag=(presentation - frame if presentation is not None and frame is not None else None),
                sensor_age_ms=None))
    evidence = dict(schema_version=1,
        interval=report['interval'], expected_interval=hashes['interval'],
        capacity=report['capacity'], apk_capacity=hashes['capacity'],
        configured_people=number(last.get('configured_people'), True),
        runtime_max_bodies=number(last.get('max_bodies'), True),
        region_count=number(last.get('region_count'), True),
        apk_sha256=report['apk_sha256'], expected_apk_sha256=hashes['apk_sha256'],
        profile_sha256=hashes['profile_sha256'], expected_profile_sha256=hashes['profile_sha256'],
        native_library_sha256=hashes['libhumanvision_sha256'],
        expected_native_library_sha256=hashes['libhumanvision_sha256'],
        model_sha256={'detector':hashes['detector_sha256'], 'body':hashes['body_sha256']},
        expected_model_sha256={'detector':hashes['detector_sha256'], 'body':hashes['body_sha256']},
        backend='NCNN Vulkan' if backend == 'backend.ncnn.vulkan' else backend,
        runtime_telemetry=runtime_telemetry,
        runtime_backend_samples=runtime_backend_samples,
        source_kind=('live_camera' if camera_seen and not video_seen else
                     'video_diagnostic' if video_seen and not camera_seen else 'mixed_or_unverified'),
        renderer_evidence_verified=False,
        copy_path='AHB Vulkan' if last.get('copy_path') == '1' else last.get('copy_path'),
        capture_provenance='UNITY_OBSERVED' if last.get('provenance') == '1' else 'UNKNOWN',
        sensor_capture_verified=False,
        cpu_full_frame_readbacks=delta('cpu_full_frame_readbacks'),
        gpu_capture_requested=delta('capture_requested'),
        bridge_no_free_slot_drops=delta('bridge_no_slot'),
        bridge_superseded_ready_drops=delta('bridge_superseded'),
        gpu_copy_errors=delta('copy_errors'), gpu_import_errors=delta('import_errors'),
        detector_max_capture_gap_ms=None, new_track_detector_age_ms=None,
        sampled_detector_age_max_ms=max((number(s.get('detector_age_ms')) or 0 for _,s in stats), default=None),
        region_revision=observations[-1]['region_revision'] if observations else None,
        max_overlay_lag_frames=number(last.get('max_overlay_lag_frames'), True),
        visible_person_count=report.get('annotated_visible_person_count'),
        warmup_seconds=5, duration_seconds=window_duration,
        observations=observations, thermal=report.get('thermal'),
        device_failures=failures, stats_series=[dict(epoch=t, **s) for t,s in stats],
        warmup_end_stats=first,
        boot_to_stream_ready_ms=((ready - boot) * 1000 if ready is not None and boot is not None else None),
        stream_ready_log_epoch=ready,
        install_identity_verified=report.get('install_identity_verified'),
        last_stats=last, package=report['package'], pid=report['pid'],
        serial=report.get('serial'), device_fingerprint=report.get('fingerprint'),
        same_pid_at_end=report['same_pid_at_end'])
    return evidence


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding='utf-8-sig'))
    evidence = summarize(report)
    output = args.report.parent / 'evidence.json'
    output.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    result = analyze(evidence)
    result['device_failures'] = evidence['device_failures']
    result['last_stats'] = evidence['last_stats']
    (args.report.parent / 'analysis.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'result':result['result'], 'failures':result['failures'], 'evidence':str(output)}))


if __name__ == '__main__':
    main()
