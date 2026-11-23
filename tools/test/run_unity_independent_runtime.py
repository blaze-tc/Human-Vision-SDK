"""Exercise the existing RuntimePrefabAcceptance Editor through UnitySkills, without screen capture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time
import urllib.request
from analyze_independent_runtime import (
    COUNT_FIELDS, RATE_FIELDS, read_progress, interval, fps, inspect_stalls, requested_control,
)


def summarize_run(records, malformed, start_us, finish_us, requested_stall_ms, controls):
    rows = [row for row, _ in records]
    window = [row for row in rows if start_us <= row['elapsed_us'] <= finish_us]
    issues = ['malformed_progress'] if malformed else []
    duration, deltas = 0, dict.fromkeys(COUNT_FIELDS)
    if len(window) < 2:
        issues.append('insufficient_measurement_samples')
    else:
        duration, deltas, endpoint_issue = interval(window[0], window[-1])
        interval_issues = [issue for first, last in zip(window, window[1:])
            if (issue := interval(first, last)[2]) is not None]
        if endpoint_issue or interval_issues:
            issues.extend(([endpoint_issue] if endpoint_issue else []) + interval_issues)
            # A reset or incoherent identity cannot produce a trusted aggregate rate.
            deltas = dict.fromkeys(COUNT_FIELDS)
        else:
            for field in (*RATE_FIELDS, 'input_errors', 'runtime_errors'):
                if deltas[field] is None:
                    issues.append('counter_unavailable:' + field)
            for field in ('input_errors', 'runtime_errors'):
                if deltas[field]:
                    issues.append('errors_during_window:' + field)
            if not deltas['native_input_frames'] or not deltas['native_fresh_frames']:
                issues.append('no_native_progress')
    requested = {field: requested_control(window, field, expected) for field, expected in controls.items()}
    for field, control in requested.items():
        if control['status'] != 'observed':
            issues.append('requested_control_' + control['status'] + ':' + field)
    stalls = inspect_stalls(rows, requested_stall_ms, controls)
    if stalls and requested_stall_ms is None:
        issues.append('unexpected_stall')
    for stall in stalls:
        if stall['status'] != 'observed':
            issues.append('stall_' + stall['status'])
    issues = list(dict.fromkeys(issues))
    return {'status': 'invalid' if issues else 'observed', 'issue': issues[0] if issues else None,
        'issues': issues, 'duration_us': duration, 'deltas': deltas, 'fps': fps(deltas, duration),
        'stalls': stalls, 'requested_controls': requested, 'malformed_lines': malformed}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--fixture-url', required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--port', type=int, default=8090)
    p.add_argument('--seconds', type=int, default=20)
    p.add_argument('--warmup', type=int, default=4)
    p.add_argument('--ready-timeout', type=int, default=120)
    p.add_argument('--target-fps', type=int, choices=(15, 30, 60), default=30)
    p.add_argument('--stall-ms', type=int, choices=(500, 2000))
    p.add_argument('--execution', choices=('native', 'compatible'), default='native')
    p.add_argument('--preview-off', action='store_true')
    p.add_argument('--gpu-passes', type=int, choices=range(17), default=0)
    p.add_argument('--cpu-workers', type=int, choices=range(5), default=0)
    p.add_argument('--diagnostics-root', type=Path)
    a = p.parse_args()
    if a.seconds < 10 or a.warmup < 0 or a.seconds + a.warmup > 3600:
        p.error('Stage duration must be10..3600 seconds with nonnegative warmup')
    project = a.project.resolve(); output = a.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    diag = a.diagnostics_root or Path(os.environ['USERPROFILE']) / 'AppData/LocalLow/DefaultCompany/Human-Vision-SDK-Test/RuntimePrefabDiagnostics'
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def call(path, body=None):
        request = urllib.request.Request(f'http://127.0.0.1:{a.port}/' + path,
            data=json.dumps(body).encode() if body is not None else None,
            headers={'Content-Type': 'application/json'})
        with opener.open(request, timeout=30) as response: return json.loads(response.read())
    health = call('health')
    if health.get('projectName') != 'Human-Vision-SDK-Test': raise RuntimeError('Refusing another Unity project')
    scene = call('skill/scene_get_info', {})['result']
    if scene.get('sceneName') != 'RuntimePrefabAcceptance' or scene.get('isDirty'): raise RuntimeError('Expected clean acceptance scene')
    state = call('skill/editor_get_state', {})['result']
    if state.get('isPlaying') or state.get('isCompiling'): raise RuntimeError('Editor must be idle and compiled')
    receipt = {'health': health, 'source_sha256': hashlib.sha256(a.source.read_bytes()).hexdigest(),
        'configuration': {key: str(value) if isinstance(value, Path) else value for key, value in vars(a).items()},
        'native_libraries': {file.name: hashlib.sha256(file.read_bytes()).hexdigest() for file in
            (project/'Assets/Plugins/x86_64').glob('humanvision*.dll')}}
    fixture = project/'Library/HumanVisionAcceptanceFixture.txt'; config = project/'Library/HumanVisionIndependentProbe.txt'
    previous = {file: file.read_bytes() if file.exists() else None for file in (fixture, config)}
    before = set(diag.glob('independent-progress-*.log')); progress = None
    receipt['summary'] = {'status': 'invalid', 'issue': 'measurement_not_completed', 'issues': ['measurement_not_completed']}
    try:
        fixture.write_text(f'{a.fixture_url}\n{a.seconds+a.warmup}\n', encoding='utf-8')
        config.write_text(f'hv_independent_execution={a.execution}\nhv_independent_preview_off={str(a.preview_off).lower()}\n'
            f'hv_independent_stall_ms={a.stall_ms or 0}\nhv_independent_stall_after_s=8\nhv_independent_target_fps={a.target_fps}\n'
            f'hv_independent_gpu_passes={a.gpu_passes}\nhv_independent_cpu_workers={a.cpu_workers}\n', encoding='utf-8')
        receipt['play'] = call('skill/editor_play', {})
        if receipt['play'].get('status') != 'success': raise RuntimeError('Unity rejected Play request')
        deadline = time.monotonic() + a.ready_timeout; ready_us = None
        while time.monotonic() < deadline:
            choices = set(diag.glob('independent-progress-*.log')) - before
            if len(choices) > 1: raise RuntimeError('Ambiguous new progress logs')
            if choices: progress = next(iter(choices))
            if progress:
                records, malformed = read_progress(progress.read_text(encoding='utf-8').splitlines())
                ready = [row for row, _ in records if row.get('streaming') and (row.get('native_fresh_frames') or 0) >= 3]
                if ready: ready_us = ready[0]['elapsed_us']; break
            time.sleep(.5)
        if ready_us is None: raise RuntimeError('No measured native progress before readiness deadline')
        print(f'RPA ready: {progress}', flush=True)
        target = ready_us + (a.warmup + a.seconds) * 1_000_000
        deadline = time.monotonic() + a.warmup + a.seconds + 30
        while time.monotonic() < deadline:
            records, malformed = read_progress(progress.read_text(encoding='utf-8').splitlines())
            if records and records[-1][0]['elapsed_us'] >= target: break
            time.sleep(.5)
        else: raise RuntimeError('Progress log did not reach the complete measurement window')
        controls = {'execution_mode': a.execution, 'preview_enabled': not a.preview_off,
            'target_fps': a.target_fps, 'actual_backend': 'cpu',
            'gpu_load_passes': a.gpu_passes, 'cpu_load_workers': a.cpu_workers}
        receipt['summary'] = summarize_run(records, malformed, ready_us + a.warmup*1_000_000,
            target, a.stall_ms, controls)
    except Exception as error:
        receipt['failure'] = f'{type(error).__name__}: {error}'
    finally:
        cleanup_issues = []
        try:
            receipt['stop'] = call('skill/editor_stop', {})
            if receipt['stop'].get('status') != 'success': cleanup_issues.append('editor_stop_failed')
        except Exception as error:
            receipt['stop'] = {'status': 'unavailable', 'error': str(error)}
            cleanup_issues.append('editor_stop_failed')
        try:
            if progress and progress.exists(): shutil.copy2(progress, output / 'independent-progress.log')
        except Exception as error:
            receipt['progress_copy_error'] = str(error)
            cleanup_issues.append('progress_copy_failed')
        try:
            receipt['console'] = call('skill/console_get_logs', {'type': 'Error', 'limit': 50})
            errors = receipt['console'].get('result', {}).get('count')
            if receipt['console'].get('status') != 'success' or type(errors) is not int or errors < 0:
                cleanup_issues.append('console_query_failed')
            elif errors:
                cleanup_issues.append('console_errors')
        except Exception as error:
            receipt['console'] = {'status': 'unavailable', 'error': str(error)}
            cleanup_issues.append('console_query_failed')
        for file, data in previous.items():
            try:
                if data is None: file.unlink(missing_ok=True)
                else: file.write_bytes(data)
            except Exception as error:
                receipt.setdefault('restore_errors', {})[file.name] = str(error)
                cleanup_issues.append('configuration_restore_failed')
        if cleanup_issues:
            summary = receipt['summary']
            summary['status'] = 'invalid'
            summary['issues'] = list(dict.fromkeys(summary['issues'] + cleanup_issues))
            summary['issue'] = summary['issues'][0]
        (output / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(receipt['summary'], ensure_ascii=False), flush=True)
        if receipt.get('failure'): print(receipt['failure'], file=sys.stderr)
    return 0 if receipt['summary']['status'] == 'observed' else 2


if __name__ == '__main__': raise SystemExit(main())
