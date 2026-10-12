"""Stage only the pinned, simulator-qualified graph candidate for private A/B.

The default manifest/profile and model are preserved. This does not approve
RK3588 device performance. All checks finish before changing the Runtime tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from stage_rknn_experimental import reindex

PINS = {
    'candidate.rknn': ('folded-output.rknn', 'be4d785b8d4c87c9339423ec894afbb35c6baf41eb5b930e4c76f00d4301c643'),
    'conversion-receipt.json': ('conversion.json', '489c9ffc5c1c9761b89c0b6b51672c9e7c63cc4793215f5bee5a23ed0a1c6d6d'),
    'simulator/comparison.json': ('comparison.json', 'f28500f793c299928de0c7e0a677d6e44d8cf2d250b82d6026c6ae3907622b10'),
}
GRAPH_SHA = '31228539a990d44618f740368e811cfbc66001ff9445791a86f2d082edd40af3'
PACK = 'yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental'


def validate(candidate, graph):
    sources = [(Path(candidate)/name, dest, sha) for name, (dest, sha) in PINS.items()]
    sources.append((Path(graph), 'graph.json', GRAPH_SHA))
    for path, _, sha in sources:
        if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
            raise ValueError('Unqualified candidate or evidence: '+str(path))
    return sources


def stage(root, candidate, graph):
    root = Path(root).resolve()
    sources = validate(candidate, graph)
    index = json.loads((root/'index.json').read_text(encoding='utf-8-sig'))
    identity = index['binding_correction']
    native, input_sha = identity['sdk_native_sha256'], identity['input_native_sha256']
    runtime_sha = index['npu_experimental']['runtime_sha256']
    pack = root/'modelpacks'/PACK
    if not (pack/'body.rknn').is_file():
        raise ValueError('Existing baseline RKNN model pack is required')
    target = pack/'candidates/folded-output'
    target.mkdir(parents=True, exist_ok=True)
    for source, name, _ in sources:
        temporary = target/(name+'.staging')
        shutil.copyfile(source, temporary)
        temporary.replace(target/name)
    reindex(root, native, input_sha, runtime_sha)
    print('Pinned folded-output candidate staged; baseline remains default; RK3588 performance unverified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-root', type=Path, required=True)
    parser.add_argument('--candidate-dir', type=Path, required=True)
    parser.add_argument('--graph-receipt', type=Path, required=True)
    args = parser.parse_args()
    stage(args.runtime_root, args.candidate_dir, args.graph_receipt)
