"""Replay the acceptance Demo's interactive controls without replacing private sample files."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil


def transform(text, expected_before, expected_after, replacements, upgrades=()):
    current = digest(text)
    if current == expected_after:
        return text
    if current != expected_before:
        upgrade = next((item for item in upgrades if item['before'] == current), None)
        if upgrade is None:
            raise ValueError('Unknown private acceptance source; refusing to overwrite it')
        replacements = upgrade['replacements']
    for before, after in replacements:
        if text.count(before) != 1:
            raise ValueError('Expected exactly one patch anchor')
        text = text.replace(before, after)
    if digest(text) != expected_after:
        raise ValueError('Patched result does not match the reviewed payload')
    return text


def digest(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def stage(project, backup, apply=False, payload_root=None):
    project, backup = Path(project).resolve(), Path(backup).resolve()
    if project.name != 'Human-Vision-SDK-Test':
        raise ValueError('Only the independent Human-Vision-SDK-Test project is supported')
    payload_root = Path(payload_root) if payload_root else Path(__file__).resolve().parent / 'unity/interactive'
    manifest = json.loads((payload_root / 'patches.json').read_text(encoding='utf-8'))
    plans = []
    for spec in manifest['patches']:
        relative = Path(spec['path'])
        target = (project / relative).resolve()
        if not target.is_relative_to(project) or target.suffix != '.cs':
            raise ValueError('Invalid acceptance patch target')
        original = target.read_bytes()
        text = original.decode('utf-8-sig').replace('\r\n', '\n')
        payload = transform(text, spec['before'], spec['after'], spec['replacements'], spec.get('upgrades', ())).encode('utf-8')
        plans.append((relative, target, original, payload))
    for spec in manifest['payloads']:
        relative = Path(spec['path'])
        target = (project / relative).resolve()
        if not target.is_relative_to(project) or target.suffix != '.cs':
            raise ValueError('Invalid acceptance payload target')
        payload = (payload_root / spec['payload']).read_bytes()
        if hashlib.sha256(payload).hexdigest() != spec['sha256']:
            raise ValueError('Acceptance payload hash does not match its reviewed manifest')
        original = target.read_bytes() if target.exists() else None
        if original is not None and original != payload and hashlib.sha256(original).hexdigest() not in spec.get('previous_sha256', []):
            raise ValueError('Unknown existing acceptance payload: ' + str(relative))
        plans.append((relative, target, original, payload))
    # Validate every source and immutable backup before touching any asset.
    for relative, target, original, payload in plans:
        saved = backup / relative
        if original is not None and original != payload and saved.exists() and saved.read_bytes() != original:
            raise ValueError('A different immutable backup already exists: ' + str(saved))
    receipt = {'project': str(project), 'applied': apply, 'files': []}
    for relative, target, original, payload in plans:
        receipt['files'].append({'path': relative.as_posix(), 'changed': original != payload,
            'previous_sha256': hashlib.sha256(original).hexdigest() if original is not None else None,
            'sha256': hashlib.sha256(payload).hexdigest()})
        if not apply or original == payload:
            continue
        if original is not None:
            saved = backup / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            if not saved.exists():
                shutil.copy2(target, saved)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--backup', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    print(json.dumps(stage(args.project, args.backup, args.apply), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
