"""Reject concrete implementation leakage and broken documentation/composition."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from generate_component_catalog import ROOT, CATALOG, components, render

def code(path):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(encoding='utf-8-sig'), flags=re.S)

def main():
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
    for path in (ROOT/'modelpacks').glob('*/manifest.json'):
        pack = json.loads(path.read_text(encoding='utf-8')); packs[pack['pack_id']] = pack
        for asset in path.parent.rglob('*'):
            if asset.suffix.lower() in ('.cs','.cpp','.c','.h','.py','.dll','.so','.exe','.ps1','.js'): errors.append('Executable modelpack content: ' + str(asset))
        for asset in pack['models']:
            file = (path.parent/asset['asset_path']).resolve()
            if not file.is_relative_to(path.parent.resolve()): errors.append('Model path escapes pack'); continue
            if not file.is_file() or hashlib.sha256(file.read_bytes()).hexdigest() != asset['sha256']: errors.append('Model hash mismatch: ' + str(file))
    for path in (ROOT/'profiles').glob('*.json'):
        if path.name == 'component.json': continue
        profile = json.loads(path.read_text(encoding='utf-8'))
        if profile.get('profile') != path.stem or profile.get('schema_version') != 1: errors.append('Invalid profile identity: ' + str(path))
        choices = [(item, 'body_pose') for item in profile.get('body_by_capacity', [profile.get('body')]) if item]
        if profile.get('hands', {}).get('enabled'): choices.append((profile['hands'], 'hand_pose'))
        for choice, capability in choices:
            plugin = by_id.get(choice['pipeline']); pack = packs.get(choice['modelPack'])
            if not plugin or not pack: errors.append('Missing profile reference: ' + str(path)); continue
            metadata = plugin[1]; caps = metadata.get('export_capabilities', {}).get(choice['pipeline'], metadata['capabilities'])
            if metadata['type'] != 'pipeline' or capability not in caps or capability not in pack['capabilities'] or pack['pipeline_id'] != choice['pipeline']:
                errors.append('Incompatible profile composition: ' + str(path))
        preferences = profile['backend']['preference']; preferences = [preferences] if isinstance(preferences,str) else preferences
        for identity in preferences:
            if identity != 'auto' and (identity not in by_id or by_id[identity][1]['type'] != 'backend'): errors.append('Missing profile backend: ' + identity)
    result = subprocess.run([sys.executable, str(ROOT/'tools/package/check_public_surface.py')], cwd=ROOT)
    if result.returncode: errors.append('Public surface guard failed')
    if errors: raise SystemExit('\n'.join(errors))
    print('Architecture/documentation boundaries: PASS')

if __name__ == '__main__': main()
