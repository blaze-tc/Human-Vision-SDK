"""Stage one immutable, locally bound TopDown profile and ModelPack."""
import argparse
import copy
import hashlib
import json
import shutil
from pathlib import Path

from tools.models.ncnn.build_local_eval_pack import PROFILE_SHA256, verify_pack
from tools.models.ncnn.model_contract import canonical_json, sha256_file


def prepare(source_pack: Path, source_profile: Path, output: Path, interval: int):
    if interval not in range(2, 7):
        raise ValueError('detector interval must be 2..6')
    if sha256_file(source_profile) != PROFILE_SHA256:
        raise ValueError('baseline profile hash drift')
    verify_pack(source_pack)
    profile = json.loads(source_profile.read_text(encoding='utf-8'))
    if profile['detector']['cadence_interval_frames'] != 4 or profile['detector']['max_capture_gap_us'] != 200000:
        raise ValueError('baseline cadence differs from eligible profile')
    profile['detector']['cadence_interval_frames'] = interval
    profile_bytes = (json.dumps(profile, indent=2, ensure_ascii=False) + '\n').encode('utf-8')
    profile_hash = hashlib.sha256(profile_bytes).hexdigest()
    target = output / 'modelpacks' / 'precision-t-26-ncnn-fp16'
    if output.exists():
        raise FileExistsError('refusing to overwrite interval evidence: ' + str(output))
    shutil.copytree(source_pack, target)
    (output / 'profiles').mkdir()
    (output / 'profiles' / 'android-ncnn-vulkan.json').write_bytes(profile_bytes)
    manifest = json.loads((target / 'modelpack.json').read_text(encoding='utf-8'))
    manifest['profile_sha256'] = profile_hash
    (target / 'modelpack.json').write_text(canonical_json(manifest), encoding='utf-8', newline='\n')
    files = sorted(p for p in target.rglob('*') if p.is_file() and p.name != 'SHA256SUMS.txt')
    (target / 'SHA256SUMS.txt').write_text(''.join(f'{sha256_file(p)}  {p.relative_to(target).as_posix()}\n' for p in files), encoding='utf-8')
    return {'interval': interval, 'profile_sha256': profile_hash,
            'modelpack_sha256': sha256_file(target / 'modelpack.json'),
            'model_sha256': {role: sha256_file(target / role / 'model.bin') for role in ('detector', 'body')}}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--interval', type=int, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.pack, args.profile, args.output, args.interval)))
