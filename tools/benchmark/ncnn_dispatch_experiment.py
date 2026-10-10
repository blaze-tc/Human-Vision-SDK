"""Prepare a private, bounded NCNN command-batching candidate, never a release.

Verify EVERY source byte against the pinned archive plus audited patch chain.
Only the low-score dispatch budget changes from 32K to 256K. CPU transition
waits, final output waits, GPU score and error handling remain unchanged.
Device numerical gates and sustained SDK runs are still required for promotion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
NET_SHA256 = '066e644a2fbb834843a5936a7c455601fc5f733c45c915d93a2868f090e2b993'
BASELINE = b'uint32_t pending_dispatch_threshold = 32 * 1024; // 32K'
CANDIDATE = b'uint32_t pending_dispatch_threshold = 256 * 1024; // private bounded 256K experiment'
POLICY_INCLUDE = b'\n#include "hv_dispatch_budget.h"'
SCOPED_CANDIDATE = b'''uint32_t pending_dispatch_threshold = humanvision::runtime::ncnn_backend::PendingDispatchBudget(
        vkdev->info.device_name(), {opt.use_fp16_packed, opt.use_fp16_storage,
                                  opt.use_fp16_arithmetic, opt.use_subgroup_ops, opt.use_packing_layout});'''
POLICY = ROOT / 'runtime/plugins/backend/ncnn/ncnn_dispatch_budget.h'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patch_net(data, adreno_only=False):
    if digest(data) != NET_SHA256:
        raise ValueError('pinned net.cpp hash mismatch')
    if data.count(BASELINE) != 1:
        raise ValueError('dispatch threshold anchor mismatch')
    if adreno_only:
        return data.replace(b'#include "net.h"', b'#include "net.h"' + POLICY_INCLUDE).replace(BASELINE, SCOPED_CANDIDATE)
    return data.replace(BASELINE, CANDIDATE)


def validate_destination(destination):
    destination = destination.resolve()
    out = (ROOT / 'out').resolve()
    if destination == out or not destination.is_relative_to(out) or destination.exists():
        raise ValueError('experiment requires a new directory strictly below workspace out')
    return destination


def prepare(source, archive, destination, adreno_only=False):
    destination = validate_destination(destination)
    pin_bytes = (ROOT / 'third_party/ncnn/provenance.json').read_bytes()
    pin = json.loads(pin_bytes)
    archive_bytes = archive.read_bytes()
    if len(archive_bytes) != pin['archive']['size'] or digest(archive_bytes) != pin['archive']['sha256']:
        raise ValueError('pinned archive size/hash mismatch')
    audited = {}
    for patch in pin['patches']:
        if digest((ROOT / patch['path']).read_bytes()) != patch['sha256']:
            raise ValueError('audited patch hash mismatch')
        for file in patch['files']:
            previous = audited.get(file['path'])
            if previous and previous['after_sha256'] != file['before_sha256']:
                raise ValueError('audited patch chain mismatch')
            audited[file['path']] = dict(first=previous['first'] if previous else file['before_sha256'],
                                        after_sha256=file['after_sha256'])
    verified = {}
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            if entry.is_dir():
                continue
            path = (source / entry.filename).resolve()
            if not path.is_relative_to(source.resolve()) or not path.is_file():
                raise ValueError('unsafe/missing cached source')
            original_hash = digest(package.read(entry))
            patch = audited.get(entry.filename)
            if patch and patch['first'] != original_hash:
                raise ValueError('patch does not start from pinned archive')
            expected = patch['after_sha256'] if patch else original_hash
            if digest(path.read_bytes()) != expected:
                raise ValueError('cached source hash mismatch: ' + entry.filename)
            verified[entry.filename] = expected
    actual_paths = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    if actual_paths != set(verified):
        raise ValueError('unexpected cached source files')
    net = patch_net((source / 'src/net.cpp').read_bytes(), adreno_only)
    shutil.copytree(source, destination / 'source')
    (destination / 'source/src/net.cpp').write_bytes(net)
    verified['src/net.cpp'] = digest(net)
    if adreno_only:
        policy = POLICY.read_bytes()
        (destination / 'source/src/hv_dispatch_budget.h').write_bytes(policy)
        verified['src/hv_dispatch_budget.h'] = digest(policy)
    # A second manifest verifies the actual COPY, not only the source cache.
    copied = {p.relative_to(destination / 'source').as_posix(): digest(p.read_bytes())
              for p in (destination / 'source').rglob('*') if p.is_file()}
    if copied != verified:
        raise ValueError('copied source manifest mismatch')
    manifest = (json.dumps(copied, indent=2) + '\n').encode()
    (destination / 'source-manifest.json').write_bytes(manifest)
    receipt = dict(kind='hv-ncnn-private-dispatch-experiment-v1', shipping_eligible=False,
                   archive_sha256=pin['archive']['sha256'], source_commit=pin['source_commit'],
                   provenance_sha256=digest(pin_bytes), source_manifest_sha256=digest(manifest),
                   baseline_net_sha256=NET_SHA256, candidate_net_sha256=digest(net),
                   pending_dispatch_budget=256 * 1024,
                   scope='adreno660-fp32' if adreno_only else 'all-low-score-devices-private-probe',
                   note='All synchronization waits retained; numerical and real SDK acceptance required.')
    receipt['script_sha256'] = digest(Path(__file__).read_bytes())
    if adreno_only:
        receipt['policy_header_sha256'] = digest(policy)
    (destination / 'experiment-source.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


def seal_install(directory):
    """Recheck source, actual build flags and archives before writing the receipt."""
    directory = directory.resolve()
    if not directory.is_relative_to((ROOT / 'out').resolve()) or directory == (ROOT / 'out').resolve():
        raise ValueError('private install must be strictly below workspace out')
    experiment = json.loads((directory / 'experiment-source.json').read_text(encoding='utf-8'))
    manifest_bytes = (directory / 'source-manifest.json').read_bytes()
    if (experiment.get('kind') != 'hv-ncnn-private-dispatch-experiment-v1' or
            experiment.get('shipping_eligible') is not False or
            digest(manifest_bytes) != experiment.get('source_manifest_sha256')):
        raise ValueError('private source manifest/receipt mismatch')
    manifest = json.loads(manifest_bytes)
    source = directory / 'source'
    actual = {p.relative_to(source).as_posix(): digest(p.read_bytes())
              for p in source.rglob('*') if p.is_file()}
    if not manifest or actual != manifest:
        raise ValueError('actual source manifest mismatch')
    pin_bytes = (ROOT / 'third_party/ncnn/provenance.json').read_bytes()
    pin = json.loads(pin_bytes)
    if digest(pin_bytes) != experiment.get('provenance_sha256'):
        raise ValueError('source provenance changed')
    cache = {}
    for line in (directory / 'build/CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        if ':' in line and '=' in line and not line.startswith('//'):
            key, value = line.split('=', 1)
            cache[key.split(':', 1)[0]] = value
    expected = dict(pin['build_flags'], ANDROID_ABI='arm64-v8a', ANDROID_PLATFORM='android-26')
    if any(cache.get(key) != value for key, value in expected.items()):
        raise ValueError('actual CMake flags differ from pinned experiment flags')
    install = directory / 'install'
    if Path(cache.get('CMAKE_INSTALL_PREFIX', '')).resolve() != install.resolve():
        raise ValueError('actual install prefix mismatch')
    libraries = []
    for path in sorted((install / 'lib').glob('*.a')):
        data = path.read_bytes()
        if not data.startswith(b'!<arch>\n'):
            raise ValueError('invalid static archive: ' + path.name)
        libraries.append(dict(path='lib/' + path.name, sha256=digest(data)))
    if len(libraries) != 7 or not (install / 'lib/libncnn.a').is_file():
        raise ValueError('expected all seven pinned static archives')
    receipt = dict(provenance_sha256=digest(pin_bytes), build_flags=expected,
                   libraries=libraries, dispatch_experiment=experiment,
                   sealing_script_sha256=digest(Path(__file__).read_bytes()))
    (directory / 'build-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--adreno-only', action='store_true', help='Use the device/precision policy tested by NcnnDispatchBudget')
    parser.add_argument('--seal-install', type=Path, help='Recheck a completed experiment build and write the SDK receipt')
    args = parser.parse_args()
    if args.seal_install:
        if args.source or args.archive or args.output or args.adreno_only:
            parser.error('--seal-install is a separate operation')
        print(json.dumps(seal_install(args.seal_install), indent=2))
    else:
        if not (args.source and args.archive and args.output):
            parser.error('prepare requires --source, --archive and --output')
        print(json.dumps(prepare(args.source, args.archive, args.output, args.adreno_only), indent=2))
