"""Hash-checked, RK3588-only standalone model test; never an SDK FPS claim.

The launcher preserves the application, settings and firmware libraries. It
uploads into a new owned /data/local/tmp directory, records errors without CPU
fallback, and applies the unchanged NCNN pose gates to actual device outputs.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from simulator_gate import read_bank, sha, uint8_rgb, evaluate, canonical_output
from yolo_pose_gate import LIMITS


def is_rk3588(properties):
    """Match a reported Rockchip SoC, not an Android version or CPU ABI."""
    return any(re.search(r'\brk3588(?:s)?(?:\b|_)', str(value).lower())
               for value in properties)


def validate_bundle(folder):
    """Validate every path/hash before permitting any device upload."""
    folder = folder.resolve()
    manifest = json.loads((folder / 'bundle.json').read_text(encoding='utf-8'))
    if manifest.get('schema_version') != 1 or not manifest.get('files'):
        raise ValueError('unsupported or empty bundle manifest')
    for name, digest in manifest['files'].items():
        path = (folder / name).resolve()
        if Path(name).is_absolute() or not path.is_relative_to(folder):
            raise ValueError('bundle path escapes its directory: ' + name)
        if not re.fullmatch('[0-9a-f]{64}', str(digest)) or sha(path) != digest:
            raise ValueError('bundle SHA mismatch: ' + name)
    if manifest.get('target') != 'rk3588' or manifest.get('input_shape') != [1, 3, 288, 512]:
        raise ValueError('bundle target/input contract mismatch')
    for key in ('probe', 'runtime', 'candidate', 'index', 'offline_report'):
        if manifest.get(key) not in manifest['files']:
            raise ValueError('bundle role is not hash checked: ' + key)
    report = json.loads((folder / manifest['offline_report']).read_text(encoding='utf-8'))
    if (report.get('offline_numerical_passed') is not True or report.get('limits') != LIMITS or
            report.get('identity', {}).get('rknn_sha256') != manifest['files'][manifest['candidate']] or
            report.get('identity', {}).get('validation_index_sha256') != manifest['files'][manifest['index']] or
            {v.get('fixture') for v in report.get('fixtures', [])} != {'seven', 'one', 'empty'} or
            len(report['fixtures']) != 3 or not all(v.get('passed') is True for v in report['fixtures'])):
        raise ValueError('candidate has no matching completed offline numerical gate')
    read_bank(folder / manifest['index'], manifest['files'][manifest['index']])
    return manifest


class Adb:
    def __init__(self, executable, serial):
        self.prefix = [str(executable), '-s', serial]

    def call(self, *args, timeout=120, checked=True):
        result = subprocess.run(self.prefix + list(args), capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=timeout)
        if checked and result.returncode:
            raise RuntimeError('ADB failed: ' + repr(args) + '\n' + result.stdout + result.stderr)
        return result


def hardware_snapshot(adb):
    # Read-only, optional telemetry. Missing/denied data is retained, never zero.
    result = {}
    for key, command in [('proc_stat', ['cat', '/proc/stat']),
                         ('thermal_service', ['dumpsys', 'thermalservice'])]:
        value = adb.call('shell', *command, checked=False)
        result[key] = dict(exit_code=value.returncode, stdout=value.stdout, stderr=value.stderr)
    return result


def benchmark_record(stdout, iterations, warmup, mask):
    records = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
    if len(records) != 1:
        raise ValueError('missing unique completed native benchmark record')
    result = records[0]
    if (result.get('completed') != iterations or result.get('warmup') != warmup or
            result.get('core_mask_set_success') != mask or
            result.get('scope') != 'repeated static input model benchmark; not SDK fresh skeleton FPS'):
        raise ValueError('actual native benchmark contract mismatch')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--iterations', type=int, default=300)
    parser.add_argument('--cores', type=int, nargs='+', choices=[1, 7], default=[1, 7])
    parser.add_argument('--run-timeout', type=int, default=600)
    args = parser.parse_args()
    if not 0 <= args.warmup <= 1000 or not 1 <= args.iterations <= 10000 or not 1 <= args.run_timeout <= 3600:
        parser.error('warmup/iterations/timeout outside bounded range')
    if len(set(args.cores)) != len(args.cores):
        parser.error('duplicate core mask')
    manifest = validate_bundle(args.bundle)
    if args.output.exists():
        parser.error('output directory already exists; preserve earlier evidence')
    args.output.mkdir(parents=True)
    adb = Adb(args.adb, args.serial)
    report = dict(scope='standalone static-image model numerical/timing test; not SDK fresh skeleton FPS',
                  device_numerical_passed=False, model_benchmark_completed=False,
                  sdk_device_performance_verified=False, deployment_ready=False,
                  bundle_sha256=sha(args.bundle / 'bundle.json'), runs=[])
    try:
        properties = {}
        for key in ('ro.soc.model', 'ro.board.platform', 'ro.hardware', 'ro.product.board',
                    'ro.product.model', 'ro.build.version.release'):
            properties[key] = adb.call('shell', 'getprop', key).stdout.strip()
        report['properties'] = properties
        if not is_rk3588(properties.values()):
            raise ValueError('device is not reported as RK3588; no RKNN upload or execution')
        bank = read_bank(args.bundle / manifest['index'], manifest['files'][manifest['index']])
        remote = '/data/local/tmp/hv-rknn-probe-' + uuid.uuid4().hex
        report['owned_remote_directory'] = remote
        report['before'] = hardware_snapshot(adb)
        adb.call('shell', 'mkdir', remote)
        for role, name in [('probe', 'probe'), ('runtime', 'runtime.so'), ('candidate', 'candidate.rknn')]:
            adb.call('push', str(args.bundle / manifest[role]), remote + '/' + name)
        adb.call('shell', 'chmod', '700', remote + '/probe')
        for name, metadata, tensor, reference in bank:
            raw_input = args.output / (name + '.rgb')
            uint8_rgb(tensor).tofile(raw_input)
            adb.call('push', str(raw_input), remote + '/' + name + '.rgb')
        for mask in args.cores:
            actual = []
            for name, *_ in bank:
                tag = name + '-core-' + str(mask)
                prefix = remote + '/' + tag
                # All shell tokens here are fixed names, a UUID, or bounded
                # integers; user-provided paths are passed only to adb push.
                run = adb.call('shell', remote + '/probe', remote + '/runtime.so',
                    remote + '/candidate.rknn', remote + '/' + name + '.rgb', prefix,
                    '512', '288', str(args.warmup), str(args.iterations), str(mask),
                    checked=False, timeout=args.run_timeout)
                (args.output / (tag + '.stdout.log')).write_text(run.stdout, encoding='utf-8')
                (args.output / (tag + '.stderr.log')).write_text(run.stderr, encoding='utf-8')
                record = dict(fixture=name, core_mask=mask, exit_code=run.returncode)
                report['runs'].append(record)
                if run.returncode:
                    raise RuntimeError('native RKNN probe failed at ' + tag + '; see preserved logs; no fallback')
                record['benchmark'] = benchmark_record(run.stdout, args.iterations, args.warmup, mask)
                output = []
                for i, columns in enumerate((65, 51)):
                    path = args.output / (tag + '-out' + str(i) + '.fp32')
                    adb.call('pull', prefix + '-out' + str(i) + '.fp32', str(path))
                    value = np.fromfile(path, dtype=np.float32)
                    if value.size != 3024 * columns:
                        raise ValueError('device raw output byte count mismatch')
                    output.append(canonical_output(value.reshape(3024, columns), 3024, columns))
                actual.append(output)
            iterator = iter(actual)
            numerical = evaluate(bank, lambda tensor: next(iterator),
                args.output / ('core-' + str(mask) + '-numerical'),
                'RK3588 RKNN device vs NCNN CPU',
                dict(rknn_sha256=manifest['files'][manifest['candidate']], core_mask=mask,
                     actual_execution='physical device', properties=properties))
            report.setdefault('core_numerical', {})[str(mask)] = numerical['offline_numerical_passed']
        report['after'] = hardware_snapshot(adb)
        report['model_benchmark_completed'] = True
        report['device_numerical_passed'] = all(report['core_numerical'].values())
        # Retain owned remote cache and all local results for diagnosis. This
        # tool neither changes firmware nor installs/modifies the Unity APK.
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
        print(report['error'], file=sys.stderr)
    finally:
        (args.output / 'device-report.json').write_text(
            json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return 0 if report['model_benchmark_completed'] and report['device_numerical_passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
