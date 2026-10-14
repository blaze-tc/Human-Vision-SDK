"""Export a complete private CPU-extended Android resource addon.

SDK/Input remain separate packages. The addon contains only the verified staged
runtime, pinned RKNN vendor library, generated deployment checker and provenance.
It never uploads resources or changes distribution/hardware qualification flags.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from package_live_sdk import metadata
from package_release_snapshot import guid, safe_relative, verify_unitypackage, write_unitypackage
import stage_main_cpu_yolo as staging

ROOT = Path(__file__).resolve().parents[2]
RUNTIME_PATH = 'Assets/StreamingAssets/HumanVision/Runtime'
ADDON_PATH = 'Assets/HumanVisionLocalCpuAndroid'
VENDOR_ASSET = 'Assets/Plugins/Android/arm64-v8a/librknnrt.so'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dependency_guids(package):
    result = set()
    for path in sorted(package.rglob('*.meta')):
        identity = guid(path.read_bytes())
        if identity in result:
            raise ValueError('Duplicate package GUID: ' + str(path))
        result.add(identity)
    return result


def verify_dependencies(sdk_package, input_package, vendor_runtime, receipt, sdk_version, input_version):
    for root, version, label in ((sdk_package, sdk_version, 'SDK'), (input_package, input_version, 'Input')):
        if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?', version):
            raise ValueError('Invalid supplied ' + label + ' package version')
        package = staging.read_json(staging.read_file(root/'package.json'), label + ' package')
        if package.get('version') != version:
            raise ValueError(label + ' package version differs from the supplied addon version')
    libraries = (
        (sdk_package, 'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so', receipt['sdk_native_sha256'], 'SDK'),
        (sdk_package, 'Runtime/Plugins/Android/arm64-v8a/libonnxruntime.so', staging.ORT_SHA256, 'ORT'),
        (sdk_package, 'Runtime/Plugins/x86_64/humanvision.dll', staging.WINDOWS_SHA256, 'Windows SDK'),
        (input_package, 'Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so', staging.INPUT_SHA256, 'Input'),
    )
    for package, name, expected, label in libraries:
        path = package/name
        if not path.resolve().is_relative_to(package):
            raise ValueError('Linked dependency path escapes package: ' + name)
        if digest(staging.read_file(path)) != expected:
            raise ValueError(label + ' native SHA/hash mismatch: ' + str(path))
        guid(staging.read_file(Path(str(path)+'.meta')))
    vendor = staging.read_file(vendor_runtime)
    if digest(vendor) != staging.VENDOR_SHA256:
        raise ValueError('RKNN runtime SHA/hash mismatch')
    sdk_ids = dependency_guids(sdk_package)
    input_ids = dependency_guids(input_package)
    if sdk_ids & input_ids:
        raise ValueError('SDK/Input dependency GUID partitions overlap')
    return vendor, sdk_ids | input_ids


def make_assets(files, vendor, receipt):
    assets = {RUNTIME_PATH+'/'+name: (data, metadata(RUNTIME_PATH+'/'+name).encode())
              for name, data in files.items()}
    assets[VENDOR_ASSET] = (vendor, metadata(VENDOR_ASSET).encode())
    setup = staging.read_file(ROOT/'tools/package/LocalCpuAndroidSetup.cs.txt').decode('utf-8')
    replacements = {
        '@INDEX_SHA256@': receipt['index_sha256'],
        '@RUNTIME_FILE_COUNT@': str(receipt['runtime_file_count']),
        '@SDK_NATIVE_SHA256@': receipt['sdk_native_sha256'],
        '@INPUT_NATIVE_SHA256@': staging.INPUT_SHA256,
        '@ORT_NATIVE_SHA256@': staging.ORT_SHA256,
        '@WINDOWS_NATIVE_SHA256@': staging.WINDOWS_SHA256,
        '@RKNN_RUNTIME_SHA256@': staging.VENDOR_SHA256,
        '@COMPOSITION_ID@': staging.COMPOSITION_ID,
    }
    for placeholder, value in replacements.items():
        if placeholder not in setup:
            raise ValueError('Missing setup template substitution: ' + placeholder)
        setup = setup.replace(placeholder, value)
    if re.search(r'@[A-Z_0-9]+@', setup):
        raise ValueError('Unresolved setup template substitution')
    setup_path = ADDON_PATH+'/Editor/HumanVisionLocalCpuAndroidSetup.cs'
    assets[setup_path] = (setup.encode(), metadata(setup_path).encode())
    readme = staging.read_file(ROOT/'tools/package/LocalCpuAndroidResources.md')
    readme_path = ADDON_PATH+'/README.md'
    assets[readme_path] = (readme, metadata(readme_path).encode())
    stage_receipt_path = ADDON_PATH+'/cpu-yolo-stage.json'
    assets[stage_receipt_path] = (staging.json_bytes(receipt), metadata(stage_receipt_path).encode())
    for name in list(assets):
        for folder in Path(name).parents:
            key = folder.as_posix()
            if key in ('.', 'Assets'):
                continue
            if key not in assets:
                assets[key] = (b'', metadata(key).replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:').encode())
    return assets, readme


def validate_asset_guids(assets, dependency_ids):
    names, ids = set(), set()
    for name, (data, meta) in assets.items():
        if not safe_relative(name) or name.casefold() in names:
            raise ValueError('Unsafe or duplicate addon asset path')
        identity = guid(meta)
        if identity in ids:
            raise ValueError('Duplicate addon asset GUID')
        names.add(name.casefold())
        ids.add(identity)
    if ids & dependency_ids:
        raise ValueError('Addon GUID partition overlaps SDK/Input packages')
    references = {value.decode() for name, (data, _) in assets.items()
                  if Path(name).suffix in ('.unity', '.prefab', '.asset')
                  for value in re.findall(rb'guid: ([0-9a-f]{32})', data) if value != b'0'*32}
    if references - ids - dependency_ids:
        raise ValueError('Unresolved addon serialized asset GUID references')
    return sorted(ids)


def build(stage_root, sdk_package, input_package, vendor_runtime, output, sdk_version, input_version):
    stage_root, sdk_package, input_package, vendor_runtime, output = (
        Path(value).resolve() for value in (stage_root, sdk_package, input_package, vendor_runtime, output))
    if output.exists():
        raise ValueError('Use a fresh new output directory')
    stage = staging.read_stage(stage_root)
    receipt = stage['receipt']
    vendor, dependency_ids = verify_dependencies(sdk_package, input_package, vendor_runtime,
                                                 receipt, sdk_version, input_version)
    assets, readme = make_assets(stage['files'], vendor, receipt)
    asset_ids = validate_asset_guids(assets, dependency_ids)
    output.mkdir(parents=True)
    archive = output/('HumanVision-LocalAndroidCpuResources-'+sdk_version+'.unitypackage')
    write_unitypackage(archive, assets)
    verify_unitypackage(archive, assets)
    manifest = {
        'schema_version': 1, 'addon_id': staging.COMPOSITION_ID,
        'sdk_version': sdk_version, 'input_version': input_version,
        'runtime_file_count': receipt['runtime_file_count'],
        'index_sha256': receipt['index_sha256'], 'source_index_sha256': staging.BASELINE_INDEX_SHA256,
        'source_receipt_sha256': staging.SOURCE_RECEIPT_SHA256,
        'stage_receipt_sha256': digest(staging.json_bytes(receipt)),
        'sdk_native_sha256': receipt['sdk_native_sha256'], 'input_native_sha256': staging.INPUT_SHA256,
        'windows_native_sha256': staging.WINDOWS_SHA256, 'onnxruntime_sha256': staging.ORT_SHA256,
        'rknn_runtime_sha256': staging.VENDOR_SHA256, 'cpu_model_sha256': staging.MODEL_SHA256,
        'local_evaluation_only': True, 'distribution_qualified': False,
        'hardware_fps_acceptance': False, 'public_upload_allowed': False,
        'archive': str(archive), 'archive_sha256': digest(archive.read_bytes()),
        'files': {name: digest(data) for name, (data, _) in assets.items() if data},
        'asset_guids': asset_ids, 'sdk_input_guid_partitions_disjoint': True,
        'sdk_input_native_assets_exported': False,
    }
    (output/'manifest.json').write_bytes(staging.json_bytes(manifest))
    (output/'README.md').write_bytes(readme)
    (output/'SHA256SUMS.txt').write_text(''.join(digest(path.read_bytes())+'  '+path.name+'\n'
        for path in sorted(output.iterdir()) if path.is_file()), encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'sdk-package-root', 'input-package-root', 'vendor-runtime', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--sdk-version', required=True)
    parser.add_argument('--input-version', required=True)
    args = parser.parse_args()
    result = build(args.stage, args.sdk_package_root, args.input_package_root, args.vendor_runtime,
                   args.output, args.sdk_version, args.input_version)
    print(json.dumps({key: result[key] for key in ('archive', 'archive_sha256', 'runtime_file_count',
        'sdk_version', 'input_version', 'hardware_fps_acceptance', 'public_upload_allowed')}, indent=2))


if __name__ == '__main__': main()
