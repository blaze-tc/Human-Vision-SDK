"""Restore reviewed UPM bytes from its verified offline Unity asset layout."""
import argparse
import hashlib
import json
import tarfile
from pathlib import Path
from package_live_sdk import metadata, VERSION, OUTPUT, sdk_assets, sdk_folders, offline_path, offline_bytes, offline_meta

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'upm/com.blazetc.humanvision'

def write_release_checksums(folder):
    files = sorted(path for path in folder.iterdir() if path.is_file() and path.name != 'SHA256SUMS.txt')
    (folder/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n' for path in files), encoding='utf-8')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path, nargs='?', default=OUTPUT / ('HumanVisionSDK-' + VERSION + '.unitypackage'))
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--dest', type=Path, default=DEST)
    args = parser.parse_args()
    dest = args.dest
    # Snapshot authority before writing, including when dest is the source UPM.
    selected = sdk_assets(args.source_root)
    paths = {offline_path(relative): relative for relative in selected}
    folders = sdk_folders(args.source_root, selected)
    folder_paths = {offline_path(relative): relative for relative in folders}
    previous_manifest = json.loads((dest/'asset-sha256.json').read_text()) if (dest/'asset-sha256.json').exists() else {}
    manifest = {}
    def write(path, data, meta=None):
        target = dest / path
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
        imported = {}
        imported_folders = set()
        for item in entries.values():
            path = item['pathname'].decode()
            if path.startswith('Assets/HumanVisionInput/'):
                # Independent dependency owns its native runtime/FFmpeg once.
                continue
            if path in folder_paths:
                relative = folder_paths[path]
                if relative in imported_folders or item['asset'] != b'' or item['asset.meta'] != offline_meta(relative, folders[relative], folder=True):
                    raise ValueError('Package differs from admitted SDK folder metadata: ' + relative)
                imported_folders.add(relative)
                continue
            if path not in paths:
                raise ValueError('Unexpected package asset: ' + path)
            relative = paths[path]
            if relative in imported:
                raise ValueError('Duplicate package asset: ' + path)
            data, meta = selected[relative]
            if item['asset'] != offline_bytes(relative, data):
                raise ValueError('Package differs from admitted SDK authority: ' + relative)
            imported[relative] = (data, meta)
        if set(imported) != set(selected):
            raise ValueError('Package is missing admitted SDK assets: ' + ', '.join(sorted(set(selected) - set(imported))))
        if imported_folders != set(folders):
            raise ValueError('Package is missing admitted SDK folder metadata.')
    # All validation is complete before destination mutation. Preserve reviewed
    # UPM bytes/GUIDs, reversing only the named offline audit-path translation.
    dest.mkdir(parents=True, exist_ok=True)
    for relative, (data, meta) in imported.items():
        write(relative, data, meta)
    for relative, meta in folders.items():
        target = dest / relative
        target.mkdir(parents=True, exist_ok=True)
        target.with_name(target.name + '.meta').write_bytes(meta)
    # Reconcile only files owned by the previous generated manifest. User caches
    # and unrelated files are never enumerated for deletion.
    for relative in sorted(set(previous_manifest) - set(manifest)):
        stale = (dest/relative).resolve()
        if not stale.is_relative_to(dest.resolve()): raise ValueError('Unsafe previous manifest path')
        for owned in (stale, stale.with_name(stale.name + '.meta')):
            if owned.is_file(): owned.unlink()
    # All folders/assets get stable metadata, including Android .androidlib directories.
    for path in sorted(dest.rglob('*')):
        if path.name.endswith('.meta'): continue
        meta = path.with_name(path.name + '.meta')
        if not meta.exists():
            relative = path.relative_to(dest).as_posix()
            value = metadata('UPM/' + relative)
            if path.is_dir(): value = value.replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:')
            meta.write_text(value, encoding='utf-8')
    write('asset-sha256.json', (json.dumps(manifest,indent=2)+'\n').encode(), metadata('UPM/asset-sha256.json').encode())
    output = args.package.parent / ('com.blazetc.humanvision-' + VERSION + '.tgz')
    with tarfile.open(output,'w:gz',format=tarfile.PAX_FORMAT) as archive:
        for path in sorted(dest.rglob('*')):
            if path.is_file(): archive.add(path,arcname='package/'+path.relative_to(dest).as_posix())
    write_release_checksums(output.parent)
    print('Git package:',dest)
    print('Local UPM archive:',output)
    print('SHA256:',hashlib.sha256(output.read_bytes()).hexdigest())

if __name__ == '__main__': main()
