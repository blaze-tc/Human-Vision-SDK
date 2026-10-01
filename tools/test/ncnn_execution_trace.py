"""Diagnostic copied-source preparation and strict ncnn execution log analysis.

GPU layer intervals and submission wall times are separate observables. Their
difference is NOT CPU overhead: intervals omit inter-layer gaps/transfer work.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
BEFORE = {'src/net.cpp': '066e644a2fbb834843a5936a7c455601fc5f733c45c915d93a2868f090e2b993',
          'src/command.cpp': '68139dd2c5d9ca0d66cad2b2513fd84b8cf662305172b756d01f85904426e105'}


def require(condition, message):
    if not condition: raise ValueError(message)


def integer(record, key, minimum=0):
    value = record.get(key)
    require(type(value) is int and value >= minimum, 'invalid integer ' + key)
    return value


def aggregate(records):
    frames, active, seen = [], None, set()
    for r in records:
        require(isinstance(r, dict), 'record must be an object')
        event, frame = r.get('event'), integer(r, 'frame', 1)
        if event == 'begin':
            require(active is None and frame not in seen, 'duplicate/nested frame')
            seen.add(frame)
            active = {'frame': frame, 'segments': {}, 'layers': set()}
            continue
        require(active is not None and active['frame'] == frame, 'orphan/mismatched frame')
        if event == 'submit':
            segment = integer(r, 'segment', 1)
            require(segment == len(active['segments']) + 1, 'segment replay or gap')
            phase = integer(r, 'phase')
            require(phase <= 2, 'invalid submission phase')
            active['segments'][segment] = dict(command=integer(r, 'command', 1), phase=phase,
                wall=integer(r, 'wall_us'), pairs=integer(r, 'pairs'), queries=[])
        elif event == 'query':
            segment = integer(r, 'segment', 1)
            require(segment in active['segments'], 'query without submit')
            s = active['segments'][segment]
            require(integer(r, 'command', 1) == s['command'], 'command mismatch')
            layer = integer(r, 'layer')
            require(layer not in active['layers'], 'duplicate layer duration')
            active['layers'].add(layer)
            require(integer(r, 'status') == 0 and integer(r, 'available_start') == 1
                    and integer(r, 'available_end') == 1, 'unavailable/failed timestamp')
            bits = integer(r, 'bits', 1)
            period = r.get('period_ns')
            require(bits <= 64 and type(period) in (int, float) and
                    math.isfinite(period) and period > 0, 'invalid timestamp capability')
            start, end = integer(r, 'start'), integer(r, 'end')
            require(start > 0 and end >= start and end < (1 << bits),
                    'missing/backward/wrapped timestamp')
            s['queries'].append((start, end, period, layer))
        elif event == 'end':
            require(integer(r, 'success') == 1, 'failed inference frame')
            require(integer(r, 'segments', 1) == len(active['segments']), 'missing submission')
            gpu, wall, layers, phase_wall = 0., 0, {}, {str(i): 0 for i in range(3)}
            for s in active['segments'].values():
                require(len(s['queries']) == s['pairs'], 'missing query pair')
                ordered = sorted(s['queries'])
                for index, (start, end, period, layer) in enumerate(ordered):
                    require(index == 0 or start >= ordered[index-1][1], 'overlapping layer intervals')
                    duration = (end-start) * period / 1000.
                    layers[str(layer)] = duration
                    gpu += duration
                wall += s['wall']
                phase_wall[str(s['phase'])] += s['wall']
            require(layers, 'no layer timestamps')
            frames.append(dict(frame=frame, gpu_layer_us=gpu, submit_wall_us=wall,
                               submissions=len(active['segments']), layers_us=layers,
                               phase_submit_wall_us=phase_wall))
            active = None
        else:
            raise ValueError('unknown/error diagnostic event ' + str(event))
    require(active is None and frames, 'truncated or empty diagnostic capture')
    return {'frames': frames, 'note': 'GPU layer intervals and submission wall time are separate; no pure CPU-overhead claim.'}


CONTEXT = r'''
#if NCNN_BENCHMARK && defined(HV_NCNN_EXECUTION_DIAGNOSTIC)
#include <time.h>
static thread_local uint64_t hv_execution_frame = 0;
static thread_local uint64_t hv_execution_segment = 0;
static thread_local bool hv_execution_enabled = false;
static thread_local int hv_execution_phase = 0;
static uint64_t hv_execution_us() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
extern "C" __attribute__((visibility("hidden"))) void hv_ncnn_execution_begin(uint64_t frame, bool enabled) {
    hv_execution_frame = frame; hv_execution_enabled = enabled; hv_execution_segment = 0; hv_execution_phase = 0;
    if (enabled) NCNN_LOGE("HVEXEC {\"event\":\"begin\",\"frame\":%llu}", (unsigned long long)frame);
}
extern "C" __attribute__((visibility("hidden"))) void hv_ncnn_execution_set_phase(int phase) { hv_execution_phase = phase; }
extern "C" __attribute__((visibility("hidden"))) void hv_ncnn_execution_end(bool success) {
    if (hv_execution_enabled) NCNN_LOGE("HVEXEC {\"event\":\"end\",\"frame\":%llu,\"segments\":%llu,\"success\":%d}",
        (unsigned long long)hv_execution_frame, (unsigned long long)hv_execution_segment, success ? 1 : 0);
    hv_execution_enabled = false;
}
#endif
'''

QUERY_SETUP = r'''
#if defined(HV_NCNN_EXECUTION_DIAGNOSTIC)
    if (!hv_execution_enabled) return;
    if (!d->query_pool) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vkdev->info.physical_device(), &count, 0);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(vkdev->info.physical_device(), &count, properties.data());
        const uint32_t family = vkdev->info.compute_queue_family_index();
        d->execution_bits = family < count ? properties[family].timestampValidBits : 0;
        if (!d->execution_bits || d->execution_bits > 64 || create_query_pool(8192) != 0) {
            NCNN_LOGE("HVEXEC {\"event\":\"error\",\"frame\":%llu}", (unsigned long long)hv_execution_frame);
            return;
        }
    }
    if (query >= d->query_count) {
        NCNN_LOGE("HVEXEC {\"event\":\"error\",\"frame\":%llu}", (unsigned long long)hv_execution_frame);
        return;
    }
    d->execution_queries.push_back(query);
#endif
'''

SUBMIT_LOG = r'''
#if NCNN_BENCHMARK && defined(HV_NCNN_EXECUTION_DIAGNOSTIC)
    if (hv_execution_enabled) {
        const uint64_t wall = hv_execution_us() - hv_execution_begun;
        const uint64_t segment = ++hv_execution_segment;
        if (d->execution_queries.size() % 2) NCNN_LOGE("HVEXEC {\"event\":\"error\",\"frame\":%llu}", (unsigned long long)hv_execution_frame);
        NCNN_LOGE("HVEXEC {\"event\":\"submit\",\"frame\":%llu,\"segment\":%llu,\"command\":%llu,\"wall_us\":%llu,\"pairs\":%u,\"phase\":%d}",
            (unsigned long long)hv_execution_frame, (unsigned long long)segment,
            (unsigned long long)(uintptr_t)d, (unsigned long long)wall, (unsigned)(d->execution_queries.size()/2), hv_execution_phase);
        for (size_t i = 0; i + 1 < d->execution_queries.size(); i += 2) {
            const uint32_t query = d->execution_queries[i];
            if (query % 2 || d->execution_queries[i+1] != query+1) NCNN_LOGE("HVEXEC {\"event\":\"error\",\"frame\":%llu}", (unsigned long long)hv_execution_frame);
            uint64_t results[4] = {0,0,0,0};
            VkResult status = vkGetQueryPoolResults(vkdev->vkdevice(), d->query_pool, query, 2,
                sizeof(results), results, 2*sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            const uint64_t mask = d->execution_bits == 64 ? UINT64_MAX : ((uint64_t(1) << d->execution_bits)-1);
            NCNN_LOGE("HVEXEC {\"event\":\"query\",\"frame\":%llu,\"segment\":%llu,\"command\":%llu,\"layer\":%u,\"start\":%llu,\"end\":%llu,\"available_start\":%llu,\"available_end\":%llu,\"status\":%d,\"bits\":%u,\"period_ns\":%.9g}",
                (unsigned long long)hv_execution_frame, (unsigned long long)segment,
                (unsigned long long)(uintptr_t)d, query/2, (unsigned long long)(results[0]&mask),
                (unsigned long long)(results[2]&mask), (unsigned long long)results[1], (unsigned long long)results[3],
                (int)status, d->execution_bits, vkdev->info.timestamp_period());
        }
    }
    d->execution_queries.clear();
#endif
'''


def replace_once(text, old, new):
    require(text.count(old) == 1, 'patch anchor drift')
    return text.replace(old, new, 1)


def patch_source(name, data):
    require(name in BEFORE and hashlib.sha256(data).hexdigest() == BEFORE[name], 'source hash drift: ' + name)
    text = data.decode()
    if name == 'src/net.cpp':
        # Retain official per-layer timestamp placements; disable legacy readers
        # and noisy CPU benchmarks in this copied diagnostic source only.
        text = text.replace('#if NCNN_BENCHMARK', '#if NCNN_BENCHMARK && !defined(HV_NCNN_EXECUTION_DIAGNOSTIC)')
        text = re.sub(r'#if NCNN_BENCHMARK && !defined\(HV_NCNN_EXECUTION_DIAGNOSTIC\)(\s+cmd.record_write_timestamp)',
                      r'#if NCNN_BENCHMARK\1', text)
    else:
        text = replace_once(text, '#include "pipeline.h"', '#include "pipeline.h"\n' + CONTEXT)
        text = replace_once(text, '    VkQueryPool query_pool;', '    VkQueryPool query_pool;\n#if defined(HV_NCNN_EXECUTION_DIAGNOSTIC)\n    std::vector<uint32_t> execution_queries;\n    uint32_t execution_bits = 0;\n#endif')
        text = replace_once(text, 'void VkCompute::record_write_timestamp(uint32_t query)\n{',
                            'void VkCompute::record_write_timestamp(uint32_t query)\n{' + QUERY_SETUP)
        text = replace_once(text, '    if (wait_semaphore && !wait_stage) return -1;',
            '    if (wait_semaphore && !wait_stage) return -1;\n#if NCNN_BENCHMARK && defined(HV_NCNN_EXECUTION_DIAGNOSTIC)\n    const uint64_t hv_execution_begun = hv_execution_us();\n#endif')
        text = replace_once(text, '    d->pending_dispatch_total = 0;\n\n    return 0;',
                            '    d->pending_dispatch_total = 0;\n' + SUBMIT_LOG + '\n    return 0;')
    return text.encode()


def prepare(destination):
    destination = destination.resolve()
    require(not destination.exists(), 'diagnostic destination must be new')
    require(destination.is_relative_to(ROOT / 'out'), 'destination must be under workspace out')
    pin_path = ROOT / 'third_party/ncnn/provenance.json'
    pin = json.loads(pin_path.read_text())
    archive = ROOT / 'out/ncnn-20260526' / pin['archive']['name']
    require(archive.stat().st_size == pin['archive']['size'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == pin['archive']['sha256'], 'archive drift')
    for patch in pin['patches']:
        require(hashlib.sha256((ROOT/patch['path']).read_bytes()).hexdigest() == patch['sha256'], 'audited patch drift')
    destination.mkdir(parents=True)
    source = destination / 'source'
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            require((source / entry.filename).resolve().is_relative_to(source), 'unsafe archive path')
        z.extractall(source)
    for patch in pin['patches']:
        subprocess.run(['git', 'apply', '--unsafe-paths', '--directory='+str(source), str(ROOT/patch['path'])], cwd=ROOT, check=True)
    hashes = {}
    for name in BEFORE:
        path = source/name
        patched = patch_source(name, path.read_bytes())
        path.write_bytes(patched)
        hashes[name] = {'before_sha256': BEFORE[name], 'after_sha256': hashlib.sha256(patched).hexdigest()}
    metadata = dict(kind='hv-ncnn-execution-diagnostic-v1', script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    source_commit=pin['source_commit'], archive_sha256=pin['archive']['sha256'], files=hashes,
                    flags={'NCNN_BENCHMARK': 'ON', 'HV_NCNN_EXECUTION_DIAGNOSTIC': 1})
    manifest = {str(p.relative_to(source)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(source.rglob('*')) if p.is_file()}
    manifest_bytes = (json.dumps(manifest, indent=2)+'\n').encode()
    (destination/'source-manifest.json').write_bytes(manifest_bytes)
    metadata['source_manifest_sha256'] = hashlib.sha256(manifest_bytes).hexdigest()
    metadata['source_file_count'] = len(manifest)
    (destination/'diagnostic-source.json').write_text(json.dumps(metadata, indent=2)+'\n')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', type=Path)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.prepare:
        print(json.dumps(prepare(args.prepare), indent=2)); return
    require(args.log and args.output, 'provide --prepare or --log/--output')
    records = [json.loads(line.split('HVEXEC ', 1)[1]) for line in args.log.read_text().splitlines() if 'HVEXEC ' in line]
    args.output.write_text(json.dumps(aggregate(records), indent=2)+'\n')


if __name__ == '__main__': main()
