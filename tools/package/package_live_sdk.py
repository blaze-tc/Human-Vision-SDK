"""Create an independent Unity import package from the admitted UPM payload.

This serializes assets; it does not launch Unity or execute tests.
"""
import argparse
import hashlib
import gzip
import io
import json
import shutil
import tarfile
import uuid
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'unity/HumanVisionDemo/Assets/HumanVision'
VERSION = '0.4.0-preview.3'
OUTPUT = ROOT / 'out/releases' / VERSION
NAMESPACE = uuid.UUID('9a1f16d6-9fe3-4b94-a3b2-77076251bfec')


def metadata(path):
    guid = uuid.uuid5(NAMESPACE, path).hex
    header = f'fileFormatVersion: 2\nguid: {guid}\n'
    if path.endswith(('.dll', '.so')):
        android = path.endswith('.so')
        preload = android and Path(path).name in ('libhumanvision.so', 'libhumanvision_input.so')
        platform = ('Android: Android\n    second:\n      enabled: 1\n      settings:\n        CPU: ARM64'
                    if android else 'Standalone: Win64\n    second:\n      enabled: 1\n      settings:\n        CPU: x86_64')
        return header + f'''PluginImporter:
  externalObjects: {{}}
  serializedVersion: 2
  iconMap: {{}}
  executionOrder: {{}}
  defineConstraints: []
  isPreloaded: {int(preload)}
  isOverridable: 0
  isExplicitlyReferenced: 0
  validateReferences: 1
  platformData:
  - first:
      Any:
    second:
      enabled: 0
      settings: {{}}
  - first:
      Editor: Editor
    second:
      enabled: {0 if android else 1}
      settings:
        CPU: x86_64
        OS: Windows
  - first:
      {platform}
  userData:
  assetBundleName:
  assetBundleVariant:
'''
    importer = 'MonoImporter' if path.endswith('.cs') else 'AssemblyDefinitionImporter' if path.endswith('.asmdef') else 'ShaderImporter' if path.endswith('.shader') else 'DefaultImporter'
    extra = '  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n  icon: {instanceID: 0}\n' if importer == 'MonoImporter' else ''
    return header + importer + ':\n  externalObjects: {}\n' + extra + '  userData:\n  assetBundleName:\n  assetBundleVariant:\n'


SDK_ROOT_FILES = ('.gitattributes', 'package.json', 'README.md', 'UPM_INSTALLATION.md',
                  'android-gpu-bridge-symbols.json')
ANDROID_SETTINGS = 'Editor/HumanVisionAndroidBuildSettings.cs'
AUDIT_FALLBACK = 'Path.Combine(Directory.GetParent(Application.dataPath).FullName, "android-gpu-bridge-symbols.json")'
OFFLINE_AUDIT_FALLBACK = 'Path.Combine(Application.dataPath, "HumanVision", "android-gpu-bridge-symbols.json")'


def offline_path(relative):
    for source, target in (('Runtime/Plugins', 'Assets/Plugins'),
                           ('RuntimeData', 'Assets/StreamingAssets/HumanVision/Runtime'),
                           ('Runtime/Demo', 'Assets/HumanVision/Demo'),
                           ('Samples~/UnifiedInput', 'Assets/HumanVision/Samples/UnifiedInput')):
        if relative == source or relative.startswith(source + '/'):
            return target + relative[len(source):]
    return 'Assets/HumanVision/' + relative


def offline_meta(relative, meta, folder=False):
    # Avoid UPM/StreamingAssets data GUID collisions across the two layouts.
    if relative == 'RuntimeData' or relative.startswith('RuntimeData/'):
        value = metadata(offline_path(relative))
        if folder:
            value = value.replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:')
        return value.encode()
    return meta


def sdk_folders(root, selected):
    """Only source-owned folders containing an admitted asset carry metadata."""
    sdk = root / 'upm/com.blazetc.humanvision'
    folders = {}
    for relative in selected:
        for parent in (sdk / relative).parents:
            if parent == sdk:
                break
            meta = parent.with_name(parent.name + '.meta')
            if meta.is_file():
                folders[parent.relative_to(sdk).as_posix()] = meta.read_bytes()
    return folders


def offline_bytes(relative, data):
    if relative != ANDROID_SETTINGS:
        return data
    old, new = AUDIT_FALLBACK.encode(), OFFLINE_AUDIT_FALLBACK.encode()
    if data.count(old) != 1:
        raise ValueError('Reviewed Android validator audit fallback must occur exactly once.')
    return data.replace(old, new)


def sdk_assets(root):
    """Read admitted UPM bytes before writing; no canonical/build/model fallback."""
    sdk = root / 'upm/com.blazetc.humanvision'
    input_descriptor = json.loads((root / 'upm/com.blazetc.humanvision.input/package.json').read_text())
    descriptor = json.loads((sdk / 'package.json').read_text())
    if descriptor['version'] != VERSION or descriptor['dependencies'].get('com.blazetc.humanvision.input') != input_descriptor['version']:
        raise ValueError('SDK version or explicit input dependency differs from admitted package.')
    required = SDK_ROOT_FILES + (ANDROID_SETTINGS, 'Editor/HumanVisionAndroidRuntimeSettings.cs',
        'Editor/HumanVisionAndroidRuntimeModeRegistry.cs', 'Editor/HumanVisionAndroidRuntimeBuildValidator.cs',
        'Editor/HumanVisionModelInstaller.cs', 'Editor/HumanVision.Editor.asmdef', 'RuntimeData/index.json',
        'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so')
    for relative in required:
        if not (sdk / relative).is_file():
            raise FileNotFoundError('Missing admitted SDK asset: ' + relative)
    audit = json.loads((sdk / 'android-gpu-bridge-symbols.json').read_text())
    native_hash = hashlib.sha256((sdk / 'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so').read_bytes()).hexdigest()
    if audit.get('native_sha256') != native_hash or audit.get('abi') != 'arm64-v8a' or audit.get('api_level') != 26 or audit.get('ncnn_vulkan_symbols_verified') is not True or audit.get('gpu_gate') is not False:
        raise ValueError('Admitted production native SHA-256/bridge audit mismatch.')
    files = [sdk / relative for relative in SDK_ROOT_FILES]
    for section in ('Runtime', 'Editor', 'RuntimeData', 'Documentation', 'Licenses', 'Samples~/UnifiedInput'):
        files.extend(source for source in sorted((sdk / section).rglob('*'))
                     if source.is_file() and not source.name.endswith('.meta'))
    assets = {}
    for source in files:
        relative = source.relative_to(sdk).as_posix()
        if source.name.startswith(('libavcodec', 'libavformat', 'libavutil', 'libswresample', 'libswscale',
                                   'avcodec-', 'avformat-', 'avutil-', 'swresample-', 'swscale-')):
            raise ValueError('Shared FFmpeg payload must be owned solely by input: ' + relative)
        meta = source.with_name(source.name + '.meta')
        assets[relative] = (source.read_bytes(), meta.read_bytes() if meta.exists() else metadata('UPM/' + relative).encode())
    # Verify the named layout translation before creating any output files.
    offline_bytes(ANDROID_SETTINGS, assets[ANDROID_SETTINGS][0])
    return assets


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=OUTPUT)
    args = parser.parse_args()
    root, output = args.source_root, args.output
    selected = sdk_assets(root)
    assets = {}
    for relative, (data, meta) in selected.items():
        path = offline_path(relative)
        assets[path] = (offline_bytes(relative, data), offline_meta(relative, meta))
    for relative, meta in sdk_folders(root, selected).items():
        assets[offline_path(relative)] = (b'', offline_meta(relative, meta, folder=True))
    input_root = root / 'upm/com.blazetc.humanvision.input'
    for source in sorted((input_root / 'Runtime').rglob('*')):
        if source.is_file() and not source.name.endswith('.meta'):
            path = 'Assets/HumanVisionInput/' + source.relative_to(input_root).as_posix()
            meta = source.with_name(source.name + '.meta')
            assets[path] = (source.read_bytes(), meta.read_bytes() if meta.exists() else metadata(path).encode())
    output.mkdir(parents=True, exist_ok=True)
    manifest = {path: hashlib.sha256(data).hexdigest() for path, (data, _) in sorted(assets.items())}
    (output / 'asset-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    package = output / ('HumanVisionSDK-' + VERSION + '.unitypackage')
    # Unity 2021's importer returns zero assets when gzip FNAME is the outer
    # .unitypackage filename. Match Unity ExportPackage's inner tar filename.
    with package.open('wb') as raw, gzip.GzipFile(filename='archtemp.tar', mode='wb',
            fileobj=raw, compresslevel=6) as compressed, tarfile.open(
            fileobj=compressed, mode='w', format=tarfile.USTAR_FORMAT) as tar:
        for path, (data, meta) in sorted(assets.items()):
            guid = next(line.split(':', 1)[1].strip() for line in meta.decode('utf-8-sig').splitlines() if line.startswith('guid:'))
            # Preserve the explicit GUID directories emitted by Unity's exporter.
            directory = tarfile.TarInfo(guid)
            directory.type = tarfile.DIRTYPE
            directory.mode = 0o755
            tar.addfile(directory)
            for leaf, payload in (('asset', data), ('asset.meta', meta), ('pathname', path.encode())):
                info = tarfile.TarInfo(guid + '/' + leaf); info.size = len(payload); info.mode = 0o644
                tar.addfile(info, io.BytesIO(payload))
    (output / 'README.md').write_bytes(selected['README.md'][0])
    shutil.copy2(root / 'docs/plans/040-v2/2026-09-10-humanvision-040-master-v2.md', output / 'IMPLEMENTATION_PLAN.md')
    archive = output / ('HumanVisionSDK-' + VERSION + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path in (package, output / 'README.md', output / 'asset-sha256.json', output / 'IMPLEMENTATION_PLAN.md'):
            bundle.write(path, path.name)
        for path in (root / 'native/include/humanvision').glob('*.h'):
            bundle.write(path, 'NativeHeaders/' + path.name)
    (output / 'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name for p in (package, archive)) + '\n')
    print(f'Created {len(assets)} assets: {package}\n{archive}\nNo Unity import or runtime tests executed.')

if __name__ == '__main__': main()
