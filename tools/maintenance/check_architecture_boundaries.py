"""Reject concrete implementation leakage and broken documentation/composition."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from generate_component_catalog import ROOT, CATALOG, components, render

def code(path):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(encoding='utf-8-sig'), flags=re.S)

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--release', action='store_true', help='reject staged or missing release dependencies')
    parser.add_argument('--package', action='store_true', help='reject staged or missing package dependencies')
    return parser.parse_args()

def main():
    args = parse_args()
    releasable = args.release or args.package
    errors = []
    items = components(); by_id = {}
    for path, item in items:
        for identity in [item['id']] + item.get('exports', []):
            if identity in by_id and by_id[identity][0] != path: errors.append('Duplicate export: ' + identity)
            by_id[identity] = (path, item)
    for path, item in items:
        for dependency in item['dependencies']:
            if dependency not in by_id: errors.append(f'{path}: missing dependency {dependency}')
    if not CATALOG.exists() or CATALOG.read_text(encoding='utf-8') != render(items): errors.append('Generated component catalog stale')
    required = ['START_HERE','CHANGE_MAP','COMPONENT_INDEX','PLUGIN_DEVELOPMENT','MODEL_PACK_GUIDE','PROFILE_GUIDE','UNITY_STABLE_API','DEBUGGING_GUIDE','RELEASE_GUIDE']
    for name in required:
        path = ROOT / 'docs/maintenance' / (name + '.md')
        if not path.exists() or len(path.read_text(encoding='utf-8')) < 100: errors.append('Missing maintenance document: ' + name)
    if not list((ROOT/'docs/maintenance/DECISIONS').glob('*.md')): errors.append('Missing architecture decisions')
    rules = [
        ('runtime/host', r'(?i)\b(?:RTMO|RTMPose|QNN|DirectML|NNAPI)\b|plugins/(?:pipeline|backend)', 'Host depends on concrete implementation'),
        ('runtime/services', r'(?i)simcc|rtmpose|rtmo|onnx|tensorview|plugins/', 'Common services contain algorithm decoding'),
        ('runtime/plugins/backend', r'HV_CANONICAL_|HV_BodyObservation|HV_HandObservation|UnityEngine', 'Backend contains pose semantics'),
        ('runtime/plugins/pipeline', r'UnityEngine|UnityEditor|\.asmdef', 'Pipeline references Unity'),
    ]
    for folder, pattern, message in rules:
        for path in (ROOT/folder).rglob('*'):
            if path.suffix in ('.h','.cpp') and re.search(pattern, code(path)): errors.append(f'{path}: {message}')
    for path in (ROOT/'runtime/plugins').rglob('*.cpp'):
        for identity in re.findall(r'"((?:pipeline|backend)\.[a-z0-9_.-]+)"', code(path)):
            if identity not in by_id: errors.append('Registered plugin lacks metadata: ' + identity)
    packs = {}
    manifest_paths = list((ROOT/'modelpacks').glob('*/manifest.json')) + list((ROOT/'modelpacks').glob('*/modelpack.json'))
    for path in manifest_paths:
        pack = json.loads(path.read_text(encoding='utf-8')); packs[pack['pack_id']] = pack
        for asset in path.parent.rglob('*'):
            if asset.suffix.lower() in ('.cs','.cpp','.c','.h','.py','.dll','.so','.exe','.ps1','.js'): errors.append('Executable modelpack content: ' + str(asset))
        for asset in pack['models']:
            files = [('asset', asset.get('asset_path'), asset.get('sha256'))]
            if pack.get('schema_version') == 2:
                files = [('param', asset.get('param_path'), asset.get('param_sha256')),
                         ('bin', asset.get('bin_path'), asset.get('bin_sha256'))]
            for name, relative, expected_hash in files:
                if not relative or not expected_hash:
                    errors.append(f'Missing model {name} path/hash: {path}')
                    continue
                file = (path.parent/relative).resolve()
                if not file.is_relative_to(path.parent.resolve()): errors.append('Model path escapes pack'); continue
                if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != expected_hash: errors.append('Model hash mismatch: ' + str(file))
    for path in (ROOT/'profiles').glob('*.json'):
        if path.name == 'component.json': continue
        profile = json.loads(path.read_text(encoding='utf-8'))
        if profile.get('profile') != path.stem or profile.get('schema_version') != 1: errors.append('Invalid profile identity: ' + str(path))
        staged = profile.get('staged_dependencies', {})
        staged_packs = set(staged.get('modelPacks', []))
        staged_backends = set(staged.get('backends', []))
        if releasable and (staged_packs or staged_backends):
            errors.append('Staged profile is not releasable/packageable: ' + str(path))
        requirements = profile.get('required_capabilities', [])
        preferences = profile['backend']['preference']; preferences = [preferences] if isinstance(preferences,str) else preferences
        if requirements and (profile['backend'].get('allow_fallback', True) or len(preferences) != 1 or preferences[0] == 'auto'):
            errors.append('Strict profile must disable fallback and select one explicit backend: ' + str(path))
        available = set()
        choices = [(item, 'body_pose') for item in profile.get('body_by_capacity', [profile.get('body')]) if item]
        if profile.get('hands', {}).get('enabled'): choices.append((profile['hands'], 'hand_pose'))
        for choice, capability in choices:
            plugin = by_id.get(choice['pipeline']); pack = packs.get(choice['modelPack'])
            if not plugin or not pack:
                missing_pack_is_staged = not pack and choice['modelPack'] in staged_packs and not releasable
                if not plugin or not missing_pack_is_staged: errors.append('Missing profile reference: ' + str(path))
                if plugin: available.update(plugin[1].get('export_capabilities', {}).get(choice['pipeline'], plugin[1]['capabilities']))
                continue
            metadata = plugin[1]; caps = metadata.get('export_capabilities', {}).get(choice['pipeline'], metadata['capabilities'])
            if metadata['type'] != 'pipeline' or capability not in caps or capability not in pack['capabilities'] or pack['pipeline_id'] != choice['pipeline']:
                errors.append('Incompatible profile composition: ' + str(path))
            available.update(caps); available.update(pack['capabilities'])
        for identity in preferences:
            if identity == 'auto': continue
            if identity not in by_id or by_id[identity][1]['type'] != 'backend':
                if identity not in staged_backends or releasable: errors.append('Missing profile backend: ' + identity)
            else: available.update(by_id[identity][1]['capabilities'])
        staged_platform_caps = {'gpu_input', 'vulkan', 'fp16-storage', 'fp16-arithmetic', 'android-hardware-buffer', 'external-sync-fd'}
        for requirement in requirements:
            supplied_by_staged_backend = staged_backends and not releasable and requirement in staged_platform_caps
            if requirement not in available and not supplied_by_staged_backend:
                errors.append(f'{path}: missing required capability {requirement}')
    result = subprocess.run([sys.executable, str(ROOT/'tools/package/check_public_surface.py')], cwd=ROOT)
    if result.returncode: errors.append('Public surface guard failed')
    if errors: raise SystemExit('\n'.join(errors))
    print('Architecture/documentation boundaries: PASS')

if __name__ == '__main__': main()
