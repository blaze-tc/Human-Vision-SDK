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

def staged_capabilities(staged, key, profile_path, errors):
    values = staged.get(key, {})
    if not isinstance(values, dict):
        errors.append(f'{profile_path}: staged_dependencies.{key} must map component IDs to capability arrays')
        return {}
    result = {}
    for identity, capabilities in values.items():
        if (not isinstance(identity, str) or not identity or
                not isinstance(capabilities, list) or not capabilities or
                any(not isinstance(capability, str) or not capability for capability in capabilities) or
                len(capabilities) != len(set(capabilities))):
            errors.append(f'{profile_path}: invalid staged capability declaration for {identity}')
            continue
        result[identity] = set(capabilities)
    return result

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
    manifest_paths = []
    for directory in sorted(path for path in (ROOT/'modelpacks').iterdir() if path.is_dir()):
        legacy = directory/'manifest.json'
        revision_two = directory/'modelpack.json'
        if legacy.is_file() and revision_two.is_file():
            errors.append(f'Ambiguous ModelPack manifests: {legacy} and {revision_two}')
            continue
        if legacy.is_file(): manifest_paths.append(legacy)
        elif revision_two.is_file(): manifest_paths.append(revision_two)
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
        staged_pipelines = staged_capabilities(staged, 'pipelines', path, errors)
        staged_packs = staged_capabilities(staged, 'modelPacks', path, errors)
        staged_backends = staged_capabilities(staged, 'backends', path, errors)
        if releasable and (staged_pipelines or staged_packs or staged_backends):
            errors.append('Staged profile is not releasable/packageable: ' + str(path))
        requirements = profile.get('required_capabilities', [])
        preferences = profile['backend']['preference']; preferences = [preferences] if isinstance(preferences,str) else preferences
        if requirements and (profile['backend'].get('allow_fallback', True) or len(preferences) != 1 or preferences[0] == 'auto'):
            errors.append('Strict profile must disable fallback and select one explicit backend: ' + str(path))
        choices = [(item, 'body_pose') for item in profile.get('body_by_capacity', [profile.get('body')]) if item]
        if profile.get('hands', {}).get('enabled'): choices.append((profile['hands'], 'hand_pose'))
        selected_pipelines = set()
        selected_packs = set()
        compositions = []
        for choice, capability in choices:
            pipeline_id = choice['pipeline']; pack_id = choice['modelPack']
            selected_pipelines.add(pipeline_id); selected_packs.add(pack_id)
            plugin = by_id.get(pipeline_id); pack = packs.get(pack_id)
            pipeline_caps = set(); pack_caps = set()
            if plugin:
                metadata = plugin[1]
                pipeline_caps.update(metadata.get('export_capabilities', {}).get(pipeline_id, metadata['capabilities']))
                if metadata['type'] != 'pipeline':
                    errors.append('Incompatible profile composition: ' + str(path))
            elif pipeline_id not in staged_pipelines or releasable:
                errors.append('Missing profile pipeline: ' + pipeline_id)
            if pack:
                pack_caps.update(pack['capabilities'])
                if pack['pipeline_id'] != pipeline_id:
                    errors.append('Incompatible profile composition: ' + str(path))
            elif pack_id not in staged_packs or releasable:
                errors.append('Missing profile ModelPack: ' + pack_id)
            if not releasable:
                pipeline_caps.update(staged_pipelines.get(pipeline_id, set()))
                pack_caps.update(staged_packs.get(pack_id, set()))
            if capability not in pipeline_caps or capability not in pack_caps:
                errors.append('Incompatible profile composition: ' + str(path))
            compositions.append((capability, pipeline_id, pipeline_caps, pack_id, pack_caps))
        selected_backends = {identity for identity in preferences if identity != 'auto'}
        backend_sources = []
        for identity in preferences:
            if identity == 'auto': continue
            module = by_id.get(identity)
            backend_caps = set()
            if module and module[1]['type'] == 'backend':
                backend_caps.update(module[1]['capabilities'])
            elif identity not in staged_backends or releasable:
                errors.append('Missing profile backend: ' + identity)
            if not releasable: backend_caps.update(staged_backends.get(identity, set()))
            if 'tensor_inference' not in backend_caps:
                errors.append(f'{path}: {identity} missing required capability tensor_inference')
            backend_sources.append((identity, backend_caps))
        for identity in staged_pipelines.keys() - selected_pipelines:
            errors.append(f'{path}: unused staged pipeline {identity}')
        for identity in staged_packs.keys() - selected_packs:
            errors.append(f'{path}: unused staged ModelPack {identity}')
        for identity in staged_backends.keys() - selected_backends:
            errors.append(f'{path}: unused staged backend {identity}')

        def require(identity, capabilities, requirement):
            if requirement not in capabilities:
                errors.append(f'{path}: {identity} missing required capability {requirement}')

        for requirement in requirements:
            if requirement in ('body_pose', 'multi_person'):
                for capability, pipeline_id, pipeline_caps, pack_id, pack_caps in compositions:
                    if capability == 'body_pose':
                        require(pipeline_id, pipeline_caps, requirement)
                        require(pack_id, pack_caps, requirement)
            elif requirement == 'hand_pose':
                hand_compositions = [composition for composition in compositions if composition[0] == 'hand_pose']
                if not hand_compositions: errors.append(f'{path}: missing required capability hand_pose')
                for _, pipeline_id, pipeline_caps, pack_id, pack_caps in hand_compositions:
                    require(pipeline_id, pipeline_caps, requirement)
                    require(pack_id, pack_caps, requirement)
            elif requirement == 'gpu_input':
                for capability, pipeline_id, pipeline_caps, pack_id, pack_caps in compositions:
                    if capability == 'body_pose':
                        require(pipeline_id, pipeline_caps, requirement)
                        require(pack_id, pack_caps, requirement)
                for identity, backend_caps in backend_sources: require(identity, backend_caps, requirement)
            elif requirement in ('tensor_inference', 'vulkan', 'fp16-storage', 'fp16-arithmetic',
                                 'android-hardware-buffer', 'external-sync-fd'):
                for identity, backend_caps in backend_sources: require(identity, backend_caps, requirement)
            else:
                available = set().union(*(composition[2] | composition[4] for composition in compositions),
                                        *(capabilities for _, capabilities in backend_sources))
                if requirement not in available: errors.append(f'{path}: missing required capability {requirement}')
    result = subprocess.run([sys.executable, str(ROOT/'tools/package/check_public_surface.py')], cwd=ROOT)
    if result.returncode: errors.append('Public surface guard failed')
    if errors: raise SystemExit('\n'.join(errors))
    print('Architecture/documentation boundaries: PASS')

if __name__ == '__main__': main()
