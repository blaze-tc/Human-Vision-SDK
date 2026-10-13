"""Export existing local Android resources, never upload or qualify redistribution.

Only the indexed model closure and pinned RKNN runtime are selected. User media,
device logs, APKs and project settings are not copied. SDK/Input remain separate.
"""
import argparse
import hashlib
import json
from pathlib import Path

from package_release_snapshot import safe_relative, write_unitypackage, verify_unitypackage
from package_live_sdk import metadata

ROOT = Path(__file__).resolve().parents[2]
RUNTIME_PATH = 'Assets/StreamingAssets/HumanVision/Runtime'
SDK_NATIVE = '8689efb872d76a825595ec4701375cf0c3c0f44d27a12d9bf2a8c9240ece4681'
INPUT_NATIVE = '0639a50ecbe765bda84a4dbc6616bd9f5fcedbf51839e2dfb7c1588650511457'
VENDOR = '07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351'
BASE_MODEL = '1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030'
PACK = 'modelpacks/yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental'


def digest(data): return hashlib.sha256(data).hexdigest()


def read_verified_runtime(root):
    """Reject unsafe/missing/changed entries before any output is created."""
    root = Path(root).resolve()
    index_bytes = (root/'index.json').read_bytes()
    receipt_bytes = (root/'staged-runtime.json').read_bytes()
    receipt = json.loads(receipt_bytes)
    if (receipt.get('schema_version') != 1 or receipt.get('stage_id') != 'local-qualified-runtime' or
            receipt.get('local_evaluation_only') is not True or receipt.get('index_sha256') != digest(index_bytes)):
        raise ValueError('Explicit stage receipt mismatch')
    entries = json.loads(index_bytes).get('files')
    if not entries: raise ValueError('Empty runtime index')
    files = {'index.json': index_bytes, 'staged-runtime.json': receipt_bytes}
    names = {n.lower() for n in files}
    for entry in entries:
        name = entry['path']
        if not safe_relative(name): raise ValueError('Unsafe runtime path')
        if name.lower() in names: raise ValueError('duplicate runtime path')
        source = root/name
        if not source.resolve().is_relative_to(root): raise ValueError('Linked runtime path escapes root')
        data = source.read_bytes()
        if digest(data) != entry['sha256']: raise ValueError('Runtime hash mismatch: ' + name)
        files[name] = data
        names.add(name.lower())
    return files


def build(project, output):
    project, output = Path(project).resolve(), Path(output).resolve()
    if output.exists(): raise ValueError('Use a new output directory')
    files = read_verified_runtime(project/RUNTIME_PATH)
    index = json.loads(files['index.json'])
    if len(index['files']) != 47: raise ValueError('Expected preview.7 47-file runtime')
    binding = index['binding_correction']
    if binding.get('sdk_native_sha256') != SDK_NATIVE or binding.get('input_native_sha256') != INPUT_NATIVE:
        raise ValueError('Runtime does not match released preview.7 binaries')
    if digest(files[PACK+'/body.rknn']) != BASE_MODEL: raise ValueError('Unpinned RKNN baseline model')
    vendor_path = project/'Packages/com.blazetc.humanvision/Runtime/Plugins/Android/arm64-v8a/librknnrt.so'
    vendor_bytes = vendor_path.read_bytes()
    if digest(vendor_bytes) != VENDOR: raise ValueError('Unpinned RKNN runtime')
    # Package public scripts only after byte verification, without invoking Unity or reading private logs.
    assets = {RUNTIME_PATH+'/'+name: (data, metadata(RUNTIME_PATH+'/'+name).encode()) for name, data in files.items()}
    plugin = 'Assets/Plugins/Android/arm64-v8a/librknnrt.so'
    assets[plugin] = (vendor_bytes, metadata(plugin).encode())
    setup = 'Assets/HumanVisionLocalAndroid/Editor/HumanVisionLocalAndroidSetup.cs'
    assets[setup] = ((ROOT/'tools/package/LocalAndroidSetup.cs.txt').read_bytes(), metadata(setup).encode())
    readme = (ROOT/'docs/user-guide/LOCAL_ANDROID_RESOURCES.md').read_bytes()
    readme_path = 'Assets/HumanVisionLocalAndroid/README.md'
    assets[readme_path] = (readme, metadata(readme_path).encode())
    for name in list(assets):
        for folder in Path(name).parents:
            key = folder.as_posix()
            if key in ('.', 'Assets'): continue
            if key not in assets:
                assets[key] = (b'', metadata(key).replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:').encode())
    output.mkdir(parents=True)
    archive = output/'HumanVision-LocalAndroidResources-preview7.unitypackage'
    write_unitypackage(archive, assets)
    verify_unitypackage(archive, assets)
    (output/'README.md').write_bytes(readme)
    manifest = dict(sdk_version='0.4.0-preview.7', input_version='0.1.0-preview.5',
        local_evaluation_only=True, distribution_qualified=False, hardware_fps_acceptance=False,
        source_index_sha256=digest(files['index.json']), runtime_file_count=47,
        files={name: digest(data) for name, (data, _) in assets.items() if data},
        archive_sha256=digest(archive.read_bytes()))
    (output/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (output/'SHA256SUMS.txt').write_text(''.join(digest(p.read_bytes())+'  '+p.name+'\n'
        for p in sorted(output.iterdir()) if p.is_file()), encoding='utf-8')
    print(json.dumps(dict(archive=str(archive), sha256=manifest['archive_sha256'], runtime_files=47,
                         public_upload_allowed=False), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.project, args.output)
