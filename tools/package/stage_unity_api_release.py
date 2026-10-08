"""Synchronize managed Unity sources, preserving the published native/model payload.

Only named source-owned managed/docs/sample directories are copied. Native/model
hashes must match preview4 before and after staging. No runtime data regeneration.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from package_live_sdk import metadata

ROOT = Path(__file__).resolve().parents[2]
SDK_VERSION = '0.4.0-preview.6'
INPUT_VERSION = '0.1.0-preview.4'
# Pin the previously verified source instead of a moving remote branch.
SHIPPING_REF = '6bdb58b0cb9b7306f43888611cdbb600e1ab4268'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    old = json.loads((ROOT/'tools/package/release-preview4-authority.json').read_text())
    for relative, expected in old['native_files'].items():
        assert digest(ROOT/'upm'/relative) == expected, relative
    sdk = ROOT/'upm/com.blazetc.humanvision'
    # Published package GUIDs are the compatibility authority. Canonical source
    # metadata can differ; do not replace existing package identities.
    legacy_meta = {}
    tree = subprocess.check_output(['git','-C',str(ROOT),'ls-tree','-rz',SHIPPING_REF,'--','upm'])
    entries = []
    for row in tree.split(b'\0'):
        if not row: continue
        info, name = row.split(b'\t', 1)
        if name.endswith(b'.meta'): entries.append((info.split()[2].decode(), name.decode()))
    proc = subprocess.Popen(['git','-C',str(ROOT),'cat-file','--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    for identity, name in entries:
        proc.stdin.write((identity+'\n').encode()); proc.stdin.flush()
        size = int(proc.stdout.readline().split()[2]); legacy_meta[name] = proc.stdout.read(size); proc.stdout.read(1)
    proc.stdin.close(); proc.wait()
    retained_settings = {p.name:p.read_bytes() for p in (sdk/'Samples~/Settings').glob('*') if p.suffix in ('.unity','.prefab') or p.name.endswith(('.unity.meta','.prefab.meta'))}
    source = ROOT/'unity/HumanVisionDemo/Assets/HumanVision'
    for part, target in [('Runtime','Runtime'), ('Demo','Runtime/Demo'), ('Editor','Editor'), ('Tests','Tests'), ('Samples/Settings','Samples~/Settings')]:
        for file in (source/part).rglob('*'):
            if not file.is_file(): continue
            relative = target+'/'+file.relative_to(source/part).as_posix()
            asset = relative.removesuffix('.meta')
            existing = relative in old['release_files'][sdk.name] or asset in old['release_files'][sdk.name]
            admitted = existing or \
                asset.startswith(('Runtime/Demo/Sdk/', 'Runtime/Demo/Settings/', 'Samples~/Settings/')) or \
                asset in ('Runtime/HumanVisionSdkConfiguration.cs','Runtime/HumanVisionSkeletonQueries.cs',
                          'Editor/HumanVisionSdkMenu.cs','Editor/HumanVisionSettingsDemoBuilder.cs',
                          'Tests/EditMode/HumanVisionSdkQueryTests.cs','Tests/EditMode/HumanVisionSdkLifecycleTests.cs',
                          'Tests/EditMode/HumanVisionUnityCompatibilityTests.cs','Tests/EditMode/HumanVisionSettingsDemoTests.cs',
                          'Tests/PlayMode/HumanVisionSdkRuntimeTests.cs')
            dest = sdk/relative
            if admitted:
                dest.parent.mkdir(parents=True, exist_ok=True)
                if existing:
                    # Keep unrelated published managed code exact, including evaluation
                    # hooks and Android release gate behavior. New API is additive.
                    dest.write_bytes(subprocess.check_output(['git','-C',str(ROOT),'show',SHIPPING_REF+':upm/'+sdk.name+'/'+relative]))
                else: shutil.copyfile(file, dest)
            elif dest.exists():
                # Only reconcile the named canonical files added by this staging
                # tool. Historical private bootstrap/GPU gates aren't shipped.
                dest.unlink()
        parent_meta = source/(part+'.meta')
        if parent_meta.exists() and (target+'.meta') in old['release_files'][sdk.name]: shutil.copyfile(parent_meta, sdk/(target+'.meta'))
    for name, data in legacy_meta.items():
        target = ROOT/name
        if target.exists(): target.write_bytes(data)
    for name, data in retained_settings.items(): (sdk/'Samples~/Settings'/name).write_bytes(data)
    # User requested a smaller product menu. Only annotations of retained
    # legacy builders change; their existing behavior/signatures remain exact.
    menus = {
        'HumanVision/Create Live Camera Demo': 'Tools/Human Vision/Legacy Examples/Create Live Camera Demo',
        'HumanVision/Create Camera Settings Scene': 'Tools/Human Vision/Legacy Examples/Create Camera Settings Scene',
        'HumanVision/Create unified Camera, Video and RTSP demos': 'Tools/Human Vision/Legacy Examples/Create demos in Assets Scenes',
        'HumanVision/Create unified demos in dedicated folder': 'HumanVision/Examples/Create Camera, Video and RTSP demos',
    }
    for file in (sdk/'Editor').glob('*.cs'):
        data = file.read_text(encoding='utf-8-sig')
        changed = data
        for old_menu, new_menu in menus.items(): changed = changed.replace('[MenuItem("'+old_menu+'")]', '[MenuItem("'+new_menu+'")]')
        if changed != data: file.write_text(changed, encoding='utf-8')
    shutil.copyfile(ROOT/'docs/user-guide/examples/HumanVisionGameplayExample.cs', sdk/'Samples~/Settings/HumanVisionGameplayExample.cs')
    shutil.copytree(ROOT/'docs/user-guide', sdk/'Documentation/user-guide', dirs_exist_ok=True)
    # Canonical checkout can use CRLF; public documentation retains exact LF.
    for document in (sdk/'Documentation/user-guide').rglob('*.md'):
        document.write_bytes(document.read_bytes().replace(b'\r\n', b'\n'))
    installation = sdk/'UPM_INSTALLATION.md'
    installation_crlf = b'\r\n' in installation.read_bytes()
    guide = installation.read_text(encoding='utf-8')
    guide = guide.replace('0.4.0-preview.5', SDK_VERSION).replace('0.1.0-preview.3', INPUT_VERSION)
    guide = guide.replace('HumanVision > Create unified demos in dedicated folder',
                          'HumanVision > Examples > Create Camera, Video and RTSP demos')
    guide = guide.replace('HumanVision > Create unified Camera, Video and RTSP demos',
                          'Tools > Human Vision > Legacy Examples > Create demos in Assets Scenes')
    guide = guide.replace('HumanVision > Input > Create standalone preview',
                          'HumanVision > Examples > Create standalone Input preview')
    installation.write_bytes((guide.replace('\n', '\r\n') if installation_crlf else guide).encode('utf-8'))
    package = json.loads((sdk/'package.json').read_text())
    package['version'] = SDK_VERSION
    package['dependencies']['com.blazetc.humanvision.input'] = INPUT_VERSION
    package['samples'] = [s for s in package['samples'] if s['path'] != 'Samples~/Settings'] + [
        {'displayName':'SDK Settings', 'description':'Editable UGUI settings scene and one-component gameplay API example.', 'path':'Samples~/Settings'}]
    (sdk/'package.json').write_text(json.dumps(package, indent=2)+'\n')
    inp = ROOT/'upm/com.blazetc.humanvision.input'
    package = json.loads((inp/'package.json').read_text()); package['version'] = INPUT_VERSION
    (inp/'package.json').write_text(json.dumps(package, indent=2)+'\n')
    # Package-local attributes preserve exact LF bytes in remote Git imports.
    for root in (sdk, inp):
        for path in sorted(root.rglob('*')):
            if path.name.endswith('.meta'): continue
            meta = Path(str(path)+'.meta')
            if not meta.exists():
                relative = path.relative_to(root).as_posix()
                value = metadata('UPM/'+root.name+'/'+relative)
                if path.is_dir(): value = value.replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:')
                meta.write_text(value)
        assets = {p.relative_to(root).as_posix():digest(p) for p in sorted(root.rglob('*'))
                  if p.is_file() and not p.name.endswith('.meta') and p.name != 'asset-sha256.json'}
        (root/'asset-sha256.json').write_text(json.dumps(assets, indent=2, sort_keys=True)+'\n')
    authority = dict(old)
    authority['sdk_version'] = SDK_VERSION; authority['input_version'] = INPUT_VERSION
    authority['baseline_candidate'] = 'v0.4.0-preview.4'
    authority['baseline_files'] = old['release_files']
    authority['release_files'] = {p.name:{f.relative_to(p).as_posix():digest(f) for f in sorted(p.rglob('*')) if f.is_file()} for p in (sdk,inp)}
    authority['baseline_deltas'] = {name:{key:{'baseline':old['release_files'][name].get(key), 'release':files.get(key)}
        for key in sorted(set(files)|set(old['release_files'][name])) if files.get(key) != old['release_files'][name].get(key)}
        for name, files in authority['release_files'].items()}
    authority['baseline_review_receipt_sha256'] = old.get('review_receipt_sha256')
    authority['review_receipt_sha256'] = digest(ROOT/'docs/reports/2026-10-08-unity-sdk-api-review.md')
    authority['review_receipt_path'] = 'docs/reports/2026-10-08-unity-sdk-api-review.md'
    authority['managed_update_note'] = 'Unity gameplay API/settings/compatibility update; all preview4 shipping native binaries and model assets retained.'
    # Only UGUI source assets plus Unity built-ins may be external. Unknown
    # references remain fatal, rather than adding missing custom scripts here.
    import re
    ui = ROOT/'out/sdk-api-verification/project/Library/PackageCache/com.unity.ugui@1.0.0'
    if ui.exists():
        known = set(old['baseline_external_asset_guids'])
        for meta in ui.rglob('*.meta'):
            match = re.search(r'^guid: ([a-f0-9]{32})$', meta.read_text(), re.M)
            if match: known.add(match[1])
        ids = set()
        references = set()
        for root in (sdk, inp):
            for meta in root.rglob('*.meta'):
                match = re.search(r'^guid: ([a-f0-9]{32})\r?$', meta.read_text(), re.M)
                if match: ids.add(match[1])
            for asset in root.rglob('*'):
                if asset.suffix in ('.unity','.prefab','.asset'):
                    references.update(re.findall(r'guid: ([a-f0-9]{32})', asset.read_text()))
        external = references - ids - {'0'*32}
        unknown = external - known
        if unknown: raise ValueError('Missing custom script GUIDs: '+str(sorted(unknown)))
        authority['baseline_external_asset_guids'] = sorted(external)
    (ROOT/'tools/package/release-preview6-authority.json').write_text(json.dumps(authority, indent=2, sort_keys=True)+'\n')
    for relative, expected in old['native_files'].items(): assert digest(ROOT/'upm'/relative) == expected
    print('Managed sources staged; all shipping native hashes preserved.')

if __name__ == '__main__': main()
