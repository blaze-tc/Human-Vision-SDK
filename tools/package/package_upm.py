"""Build Git UPM directory and local UPM tgz from the exact Unity release assets."""
import argparse
import hashlib
import io
import json
import tarfile
from pathlib import Path
from package_live_sdk import metadata, NAMESPACE, VERSION, OUTPUT
import uuid

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'upm/com.blazetc.humanvision'

def write_release_checksums(folder):
    files = sorted(path for path in folder.iterdir() if path.is_file() and path.name != 'SHA256SUMS.txt')
    (folder/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n' for path in files), encoding='utf-8')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path, nargs='?', default=OUTPUT / ('HumanVisionSDK-' + VERSION + '.unitypackage'))
    args = parser.parse_args()
    DEST.mkdir(parents=True, exist_ok=True)
    previous_manifest = json.loads((DEST/'asset-sha256.json').read_text()) if (DEST/'asset-sha256.json').exists() else {}
    manifest = {}
    def write(path, data, meta=None):
        target = DEST / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        manifest[path] = hashlib.sha256(data).hexdigest()
        if meta is not None:
            target.with_name(target.name + '.meta').write_bytes(meta)
    with tarfile.open(args.package, 'r:gz') as archive:
        # Stream members once: repeated backward seeks through compressed native libs are expensive.
        entries = {}
        for member in archive:
            if member.isfile():
                guid, leaf = member.name.split('/', 1)
                entries.setdefault(guid, {})[leaf] = archive.extractfile(member).read()
        for item in entries.values():
            path = item['pathname'].decode()
            if path.startswith('Assets/HumanVision/'):
                relative = path[len('Assets/HumanVision/'):]
                if relative.startswith('Demo/'): relative = 'Runtime/' + relative
            elif path.startswith('Assets/Plugins/'):
                relative = 'Runtime/Plugins/' + path[len('Assets/Plugins/'):]
            elif path.startswith('Assets/StreamingAssets/HumanVision/Models/'):
                relative = 'Models/' + Path(path).name
            elif path.startswith('Assets/StreamingAssets/HumanVision/Runtime/'):
                relative = 'RuntimeData/' + path[len('Assets/StreamingAssets/HumanVision/Runtime/'):]
            else:
                raise ValueError('Unexpected package asset: ' + path)
            # StreamingAssets may already contain the unitypackage copy. Package
            # models are independent file sources, never scene asset references.
            model_meta = metadata('UPM/' + relative).encode() if relative.startswith(('Models/', 'RuntimeData/')) else item['asset.meta']
            write(relative, item['asset'], model_meta)
    # Unity may materialize this subfolder without the repository root attributes.
    write('.gitattributes', b'* -text\n')
    descriptor = dict(name='com.blazetc.humanvision', version=VERSION, displayName='Human Vision SDK',
        unity='2021.3', description='Independent camera skeleton SDK: Windows x64 and Android ARM64, numbered regions and hand endpoints.',
        dependencies={key:'1.0.0' for key in ('com.unity.ugui','com.unity.modules.physics','com.unity.modules.imageconversion',
            'com.unity.modules.imgui','com.unity.modules.jsonserialize','com.unity.modules.unitywebrequest','com.unity.modules.video')}, author={'name':'blaze-tc'},
        repository={'type':'git','url':'https://github.com/blaze-tc/Human-Vision-SDK.git'},
        documentationUrl='https://github.com/blaze-tc/Human-Vision-SDK/blob/main/docs/UPM_INSTALLATION.md')
    write('package.json', (json.dumps(descriptor, indent=2)+'\n').encode())
    editor = {'name':'HumanVision.Editor','references':['HumanVision.Runtime','HumanVision.Demo','UnityEngine.UI'],'includePlatforms':['Editor'],'autoReferenced':True}
    write('Editor/HumanVision.Editor.asmdef', json.dumps(editor,indent=2).encode(),metadata('UPM/Editor/HumanVision.Editor.asmdef').encode())
    installer = ROOT/'tools/package/HumanVisionModelInstaller.cs'
    write('Editor/HumanVisionModelInstaller.cs', installer.read_bytes(), metadata('UPM/Editor/HumanVisionModelInstaller.cs').encode())
    write('UPM_INSTALLATION.md', (ROOT/'docs/UPM_INSTALLATION.md').read_bytes())
    # Reconcile only files owned by the previous generated manifest. User caches
    # and unrelated files are never enumerated for deletion.
    for relative in sorted(set(previous_manifest) - set(manifest)):
        stale = (DEST/relative).resolve()
        if not stale.is_relative_to(DEST.resolve()): raise ValueError('Unsafe previous manifest path')
        for owned in (stale, stale.with_name(stale.name + '.meta')):
            if owned.is_file(): owned.unlink()
    # All folders/assets get stable metadata, including Android .androidlib directories.
    for path in sorted(DEST.rglob('*')):
        if path.name.endswith('.meta'): continue
        meta = path.with_name(path.name + '.meta')
        if not meta.exists():
            relative = path.relative_to(DEST).as_posix()
            value = metadata('UPM/' + relative)
            if path.is_dir(): value = value.replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:')
            meta.write_text(value, encoding='utf-8')
    write('asset-sha256.json', (json.dumps(manifest,indent=2)+'\n').encode(), metadata('UPM/asset-sha256.json').encode())
    output = args.package.parent / ('com.blazetc.humanvision-' + VERSION + '.tgz')
    with tarfile.open(output,'w:gz',format=tarfile.PAX_FORMAT) as archive:
        for path in sorted(DEST.rglob('*')):
            if path.is_file(): archive.add(path,arcname='package/'+path.relative_to(DEST).as_posix())
    write_release_checksums(output.parent)
    print('Git package:',DEST)
    print('Local UPM archive:',output)
    print('SHA256:',hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == '__main__': main()
