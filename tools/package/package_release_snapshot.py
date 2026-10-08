"""Build reproducible release archives from a pinned, reviewed two-package snapshot.

No build directories, downloads, canonical fallbacks or Unity mutations are used.
Run --verify-only first; output directories are immutable and must not exist.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

from package_live_sdk import metadata, offline_path, offline_bytes, offline_meta

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ('com.blazetc.humanvision', 'com.blazetc.humanvision.input')
VERSION = '0.4.0-preview.4'
INPUT_VERSION = '0.1.0-preview.2'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    def pairs(rows):
        result = {}
        for key, value in rows:
            if key in result:
                raise ValueError('Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(path.read_text(encoding='utf-8-sig'), object_pairs_hook=pairs)


def file_hashes(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Linked snapshot path: ' + str(path))
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha256(path.read_bytes())
    return result


def safe_relative(name):
    return (isinstance(name, str) and name and not name.startswith('/') and
            not any(c in name for c in (':', '\\')) and
            all(part not in ('', '.', '..') for part in name.split('/')))


def verify_file_closure(root, expected):
    if not all(safe_relative(name) for name in expected):
        raise ValueError('Unsafe source authority path')
    actual = file_hashes(root)
    if actual != expected:
        changed = sorted(name for name in set(actual) | set(expected)
                         if actual.get(name) != expected.get(name))
        raise ValueError('Immutable source file/hash closure differs: ' + ', '.join(changed))


def guid(meta):
    matches = re.findall(rb'^guid: ([0-9a-f]{32})\r?$', meta, re.M)
    if len(matches) != 1:
        raise ValueError('Metadata requires exactly one GUID')
    return matches[0].decode()


def is_folder_meta(meta):
    markers = re.findall(rb'^folderAsset: ([^\r\n]*)\r?$', meta, re.M)
    if markers and markers != [b'yes']:
        raise ValueError('Invalid folder metadata declaration')
    return bool(markers)


def validate_guids(packages, external):
    ids, references = {}, set()
    for package in packages:
        for path in package.rglob('*.meta'):
            identity = guid(path.read_bytes())
            if identity in ids:
                raise ValueError('Duplicate package GUID: ' + identity)
            ids[identity] = str(path)
        for path in package.rglob('*'):
            if path.suffix in ('.unity', '.prefab', '.asset'):
                references.update(value.decode() for value in
                                  re.findall(rb'guid: ([0-9a-f]{32})', path.read_bytes())
                                  if value != b'0' * 32)
    unresolved = references - set(ids)
    if unresolved != external:
        raise ValueError('Unresolved serialized GUID closure differs: ' + str(sorted(unresolved)))
    return len(ids)


def audit_native_dependencies(root):
    """Fresh actual ELF imports/API notes and PE sibling/platform owners."""
    from tools.test.verify_android_native import parse_dynamic_symbols, validate_dynamic_closure
    ndk = Path('D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK')
    tools = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
    if not (tools / 'llvm-readelf.exe').is_file():
        raise FileNotFoundError('Pinned native dependency auditor unavailable: ' + str(tools))
    upm = root / 'upm'
    packaged = [upm / name / 'Runtime/Plugins/Android/arm64-v8a' for name in PACKAGES]
    system = [ndk / 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26']
    def run(tool, *args):
        return subprocess.check_output([str(tools / tool), *map(str, args)], text=True, encoding='utf-8')
    def symbols(path):
        return parse_dynamic_symbols(run('llvm-readelf.exe', '--dyn-syms', '--wide', path))
    def needed(path):
        return re.findall(r'Shared library: \[([^]]+)\]', run('llvm-readelf.exe', '-d', path))
    android = []
    for directory in packaged:
        for path in sorted(directory.glob('*.so')):
            dependencies = needed(path)
            resolved = validate_dynamic_closure(path, dependencies, packaged, system, symbols, needed)
            if path.name in ('libhumanvision.so', 'libhumanvision_input.so'):
                notes = run('llvm-readelf.exe', '-h', '-n', path)
                note = re.search(r'\.note.android.ident.*?description data:\s*((?:[0-9a-f]{2} ){4})', notes, re.S)
                if not note or int.from_bytes(bytes.fromhex(note[1]), 'little') != 26:
                    raise ValueError('Native API26 note differs')
            if path.name == 'libhumanvision_input.so' and any(re.search(r'onnx|ncnn|humanvision\.so', dep, re.I) for dep in dependencies):
                raise ValueError('Input ELF inference dependency')
            android.append({'path': path.relative_to(upm).as_posix(), 'needed': dependencies,
                            'resolved_strong_imports': resolved})
    sdk = packaged[0] / 'libhumanvision.so'
    if not {'libandroid.so', 'libvulkan.so', 'liblog.so', 'libonnxruntime.so'}.issubset(needed(sdk)):
        raise ValueError('SDK Android GPU dependency closure differs')
    defined = run('llvm-nm.exe', '--defined-only', '--demangle', sdk)
    for symbol in ('ncnn::VulkanDevice::', 'ncnn::VkCompute::record_import_android_hardware_buffer(',
                   'VkImageLayout, unsigned int, unsigned int', 'vkCreateInstance'):
        if symbol not in defined:
            raise ValueError('Missing ncnn/Vulkan/AHB symbol: ' + symbol)
    libraries = {}
    for name in PACKAGES:
        for path in sorted((upm / name / 'Runtime/Plugins/x86_64').glob('*.dll')):
            if path.name.lower() in libraries:
                raise ValueError('Duplicate Windows native owner')
            libraries[path.name.lower()] = path
    windows = []
    for path in libraries.values():
        dependencies = re.findall(r'^\s*Name: (.+)$', run('llvm-readobj.exe', '--coff-imports', path), re.M)
        if not dependencies:
            raise ValueError('Empty Windows native dependency audit')
        for name in dependencies:
            if name.lower() in libraries or re.fullmatch(r'(?:api-ms-win-[a-z0-9-]+|ext-ms-win-[a-z0-9-]+)\.dll', name, re.I):
                continue
            system_path = Path(os.environ['WINDIR']) / 'System32' / name
            data = system_path.read_bytes()
            offset = int.from_bytes(data[60:64], 'little')
            if data[:2] != b'MZ' or data[offset:offset+4] != b'PE\0\0' or int.from_bytes(data[offset+4:offset+6], 'little') != 0x8664:
                raise ValueError('Windows platform dependency is not x64 PE: ' + name)
        if path.name == 'humanvision_input.dll' and any(re.search(r'onnx|ncnn|^humanvision\.dll$', dep, re.I) for dep in dependencies):
            raise ValueError('Input Windows inference dependency')
        windows.append({'path': path.relative_to(upm).as_posix(), 'needed': dependencies,
                        'packaged_import_owners': {name: libraries[name.lower()].relative_to(upm).as_posix()
                                                  for name in dependencies if name.lower() in libraries}})
    return {'android': android, 'windows': windows}


def validate_snapshot(root, authority):
    upm = root / 'upm'
    observed = {name: file_hashes(upm / name) for name in PACKAGES}
    for name in PACKAGES:
        verify_file_closure(upm / name, authority['release_files'][name])
    packages = [upm / name for name in PACKAGES]
    sdk, inp = packages
    count = validate_guids(packages, set(authority['baseline_external_asset_guids']))
    descriptor, input_descriptor = [read_json(p / 'package.json') for p in packages]
    if (descriptor['name'] != PACKAGES[0] or descriptor['version'] != VERSION or
            input_descriptor['name'] != PACKAGES[1] or input_descriptor['version'] != INPUT_VERSION or
            descriptor['dependencies'][PACKAGES[1]] != INPUT_VERSION):
        raise ValueError('Pinned release versions/dependency differ')
    assemblies = {}
    for package in packages:
        for path in package.rglob('*'):
            if path.is_file() and not path.name.endswith('.meta'):
                if not Path(str(path) + '.meta').is_file():
                    raise ValueError('Missing metadata: ' + str(path))
            if path.is_dir() and not any(x.endswith('~') for x in path.relative_to(package).parts):
                if not Path(str(path) + '.meta').is_file():
                    raise ValueError('Missing folder metadata: ' + str(path))
            if path.suffix == '.asmdef':
                value = read_json(path)
                if value['name'] in assemblies:
                    raise ValueError('Duplicate assembly')
                assemblies[value['name']] = value
        assets = {name: value for name, value in observed[package.name].items()
                  if not name.endswith('.meta') and name != 'asset-sha256.json'}
        if read_json(package / 'asset-sha256.json') != assets:
            raise ValueError('Asset hash index closure differs: ' + package.name)
    external_assemblies = {'UnityEngine.UI', 'Unity.ugui', 'UnityEditor.TestRunner', 'UnityEngine.TestRunner'}
    for name, definition in assemblies.items():
        for ref in definition.get('references', []):
            if ref not in assemblies and ref not in external_assemblies:
                raise ValueError('Unresolved assembly reference: ' + ref)
            if name.startswith('HumanVision.Input') and ref in ('HumanVision.Runtime', 'HumanVision.Demo'):
                raise ValueError('Input references SDK inference')
    if any(p.suffix in ('.onnx', '.bin', '.param') for p in inp.rglob('*')):
        raise ValueError('Input contains models')
    for relative, expected in authority['native_files'].items():
        path = upm / relative
        if sha256(path.read_bytes()) != expected:
            raise ValueError('Native identity differs: ' + relative)
        meta = Path(str(path) + '.meta').read_text(encoding='utf-8-sig')
        if 'PluginImporter:' not in meta or 'Any:' not in meta or 'enabled: 0' not in meta:
            raise ValueError('Invalid native importer')
        data = path.read_bytes()
        if path.suffix == '.so':
            if data[:4] != b'\x7fELF' or data[4] != 2 or int.from_bytes(data[18:20], 'little') != 183 or 'CPU: ARM64' not in meta:
                raise ValueError('Expected Android ARM64 ELF/importer')
        else:
            offset = int.from_bytes(data[60:64], 'little')
            if data[:2] != b'MZ' or data[offset:offset+4] != b'PE\0\0' or int.from_bytes(data[offset+4:offset+6], 'little') != 0x8664 or 'CPU: x86_64' not in meta:
                raise ValueError('Expected Windows x64 PE/importer')
    native = authority['native_files']
    if sum(p.endswith('.so') for p in native) != 8 or sum(p.endswith('.dll') for p in native) != 9:
        raise ValueError('Expected qualified eight SO/nine DLL closure')
    sys.path.insert(0, str(root))
    from tools.models.ncnn.stage_model_input_qualities import validate_runtime
    from check_input_package import check
    validate_runtime(sdk / 'RuntimeData')
    errors = check(inp)
    if errors:
        raise ValueError('\n'.join(errors))
    native_dependencies = audit_native_dependencies(root)
    audit = read_json(sdk / 'android-gpu-bridge-symbols.json')
    runtime = read_json(sdk / 'RuntimeData/index.json')
    if (audit['native_sha256'] != native[PACKAGES[0] + '/Runtime/Plugins/Android/arm64-v8a/libhumanvision.so'] or
            audit['abi'] != 'arm64-v8a' or audit['api_level'] != 26 or
            audit['ncnn_vulkan_symbols_verified'] is not True or audit['gpu_gate'] is not False or
            runtime['binding_correction']['distribution_qualified'] is not False or
            runtime['binding_correction']['hardware_fps_acceptance'] is not False):
        raise ValueError('Native audit/evaluation markers differ')
    return {'result': 'PASS', 'files': sum(map(len, observed.values())), 'guids': count,
            'android_so': 8, 'windows_dll': 9, 'runtime_index_files': len(runtime['files']),
            'distribution_qualified': False, 'hardware_fps_acceptance': False,
            'native_dependencies': native_dependencies}


def input_path(relative):
    if relative == 'Runtime/Plugins' or relative.startswith('Runtime/Plugins/'):
        return 'Assets/Plugins' + relative[len('Runtime/Plugins'):]
    if relative == 'Samples~' or relative.startswith('Samples~/'):
        return 'Assets/HumanVisionInput/Samples' + relative[len('Samples~'):]
    return 'Assets/HumanVisionInput/' + relative


def offline_assets(root):
    assets, translations = {}, []
    for name in PACKAGES:
        package = root / 'upm' / name
        for relative in file_hashes(package):
            if relative.endswith('.meta'):
                continue
            data, meta = (package / relative).read_bytes(), (package / (relative + '.meta')).read_bytes()
            if name == PACKAGES[0]:
                dest, exported, exported_meta = offline_path(relative), offline_bytes(relative, data), offline_meta(relative, meta)
            else:
                dest, exported, exported_meta = input_path(relative), data, meta
            if dest in assets:
                raise ValueError('Duplicate offline asset path')
            assets[dest] = (exported, exported_meta)
            translations.append({'package': name, 'source': relative, 'destination': dest,
                                 'source_sha256': sha256(data), 'asset_sha256': sha256(exported),
                                 'source_meta_sha256': sha256(meta), 'asset_meta_sha256': sha256(exported_meta)})
    for dest in list(assets):
        for parent in Path(dest).parents:
            name = parent.as_posix()
            if name not in ('.', 'Assets') and name not in assets:
                assets[name] = (b'', metadata(name).replace('DefaultImporter:', 'folderAsset: yes\nDefaultImporter:').encode())
    ids = [guid(meta) for data, meta in assets.values()]
    if len(ids) != len(set(ids)):
        raise ValueError('Duplicate offline GUID')
    # Native/plugin ownership and script GUIDs are preserved; only RuntimeData
    # GUIDs and the reviewed Android audit-path fallback are translated.
    return assets, translations


def split_offline_assets(root):
    """Partition the complete offline layout, assigning shared folders to Input.

    Input is imported first. Each GUID appears in exactly one archive, including
    generated shared native-plugin ancestors; the union retains every byte of
    the existing complete offline export.
    """
    assets, translations = offline_assets(root)
    owners = {item['destination']: item['package'] for item in translations}
    # Visit Input first so a shared generated ancestor has a single stable owner.
    for package in reversed(PACKAGES):
        for item in translations:
            if item['package'] != package:
                continue
            for parent in Path(item['destination']).parents:
                name = parent.as_posix()
                if name in assets:
                    owners.setdefault(name, package)
    if set(owners) != set(assets):
        raise ValueError('Split offline asset ownership closure differs')
    partitions = {package: {name: value for name, value in assets.items()
                            if owners[name] == package} for package in PACKAGES}
    return partitions, translations


def add_member(archive, name, payload):
    info = tarfile.TarInfo(name)
    info.size, info.mode, info.mtime = len(payload), 0o644, 0
    archive.addfile(info, io.BytesIO(payload))


def write_tgz(path, files):
    with path.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0, compresslevel=6) as compressed, tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as archive:
        for name, data in sorted(files.items()):
            add_member(archive, name, data)


def verify_tgz(path, expected):
    observed = {}
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            if not member.isfile() or not safe_relative(member.name) or member.name in observed:
                raise ValueError('Unsafe or duplicate tar member')
            observed[member.name] = archive.extractfile(member).read()
    if observed != expected:
        raise ValueError('TGZ byte closure differs')


def write_unitypackage(path, assets):
    with path.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='archtemp.tar', mtime=0, compresslevel=6) as compressed, tarfile.open(fileobj=compressed, mode='w', format=tarfile.USTAR_FORMAT) as archive:
        for name, (data, meta) in sorted(assets.items()):
            identity = guid(meta)
            directory = tarfile.TarInfo(identity)
            directory.type, directory.mode = tarfile.DIRTYPE, 0o755
            archive.addfile(directory)
            folder = is_folder_meta(meta)
            if folder and data != b'':
                raise ValueError('Unity folder cannot have file asset bytes')
            # Unity 2021.3 ExportPackage emits no regular "asset" member for
            # folderAsset groups. A zero-byte regular member creates a file at
            # the folder path, causing the actual importer to fail copying it.
            members = [('asset.meta', meta), ('pathname', name.encode())]
            if not folder:
                members.insert(0, ('asset', data))
            for leaf, payload in members:
                add_member(archive, identity + '/' + leaf, payload)


def verify_unitypackage(path, expected):
    raw = path.read_bytes()
    if raw[:2] != b'\x1f\x8b' or not raw[3] & 8 or raw[10:].split(b'\0', 1)[0] != b'archtemp.tar':
        raise ValueError('Unity gzip filename contract differs')
    groups, directories = {}, set()
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            if member.isdir():
                identity = member.name.rstrip('/')
                if not re.fullmatch('[0-9a-f]{32}', identity) or identity in directories:
                    raise ValueError('Invalid Unity GUID directory')
                directories.add(identity)
                continue
            if not member.isfile() or '/' not in member.name:
                raise ValueError('Invalid Unity archive member')
            identity, leaf = member.name.split('/', 1)
            if not re.fullmatch('[0-9a-f]{32}', identity) or leaf not in ('asset', 'asset.meta', 'pathname') or leaf in groups.setdefault(identity, {}):
                raise ValueError('Invalid or duplicate Unity member')
            groups[identity][leaf] = archive.extractfile(member).read()
    actual = {}
    for identity, item in groups.items():
        if identity not in directories or not {'asset.meta', 'pathname'}.issubset(item) or guid(item['asset.meta']) != identity:
            raise ValueError('Unity GUID group closure differs')
        folder = is_folder_meta(item['asset.meta'])
        leaves = {'asset.meta', 'pathname'} if folder else {'asset', 'asset.meta', 'pathname'}
        if set(item) != leaves:
            raise ValueError('Unity folder member closure differs' if folder else 'Unity file member closure differs')
        name = item['pathname'].decode()
        if not safe_relative(name) or name in actual:
            raise ValueError('Invalid or duplicate Unity pathname')
        actual[name] = (b'' if folder else item['asset'], item['asset.meta'])
    if directories != set(groups) or actual != expected:
        raise ValueError('Unity archive asset/meta byte closure differs')


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def build(root, output, authority):
    validation = validate_snapshot(root, authority)
    partitions, translations = split_offline_assets(root)
    assets = {name: value for partition in partitions.values() for name, value in partition.items()}
    ids = {guid(meta) for data, meta in assets.values()}
    references = {value.decode() for name, (data, meta) in assets.items()
                  if Path(name).suffix in ('.unity', '.prefab', '.asset')
                  for value in re.findall(rb'guid: ([0-9a-f]{32})', data) if value != b'0' * 32}
    if references - ids != set(authority['baseline_external_asset_guids']):
        raise ValueError('Offline serialized prefab/sample GUID closure differs')
    if output.exists():
        raise FileExistsError('Use a fresh immutable output directory: ' + str(output))
    output.mkdir(parents=True)
    generated = []
    for name, version in zip(PACKAGES, (VERSION, INPUT_VERSION)):
        package = root / 'upm' / name
        files = {'package/' + rel: (package / rel).read_bytes() for rel in file_hashes(package)}
        path = output / (name + '-' + version + '.tgz')
        write_tgz(path, files)
        verify_tgz(path, files)
        generated.append(path)
    unity_names = ('HumanVisionSDK-' + VERSION + '.unitypackage',
                   'HumanVisionInput-' + INPUT_VERSION + '.unitypackage')
    for package, name in zip(PACKAGES, unity_names):
        path = output / name
        write_unitypackage(path, partitions[package])
        verify_unitypackage(path, partitions[package])
        generated.append(path)
    (output / 'README.md').write_bytes((root / 'upm' / PACKAGES[0] / 'UPM_INSTALLATION.md').read_bytes())
    write_json(output / 'asset-sha256.json', {name: {'kind': 'folder' if is_folder_meta(meta) else 'file',
                                                   'asset': None if is_folder_meta(meta) else sha256(data),
                                                   'meta': sha256(meta)}
                                            for name, (data, meta) in sorted(assets.items())})
    git_head = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    provenance = {'schema_version': 1, 'source_commit': git_head, 'authority': authority,
                  'authority_sha256': sha256((root / 'tools/package/release-preview4-authority.json').read_bytes()),
                  'packaging_source_sha256': {name: sha256((root / 'tools/package' / name).read_bytes()) for name in
                                             ('package_release_snapshot.py', 'package_live_sdk.py', 'check_input_package.py')},
                  'validation': validation, 'unity_layout_translation': translations,
                  'unity_import_order': list(reversed(unity_names)),
                  'unity_archive_partitions': {
                      name: {'package': package, 'assets': len(partitions[package]),
                             'guids': sorted(guid(meta) for data, meta in partitions[package].values())}
                      for package, name in zip(PACKAGES, unity_names)},
                  'source_snapshot_note': 'Exact package bytes are pinned independently of Git HEAD; the commit identifies the checkout used.',
                  'hardware_fps_acceptance': False, 'distribution_qualified': False}
    write_json(output / 'source-snapshot.json', provenance)
    generated += [output / 'README.md', output / 'asset-sha256.json', output / 'source-snapshot.json']
    (output / 'SHA256SUMS.txt').write_text(''.join(sha256(p.read_bytes()) + '  ' + p.name + '\n' for p in sorted(generated)), encoding='utf-8')
    return {'validation': validation, 'artifacts': {p.name: sha256(p.read_bytes()) for p in generated}, 'unity_assets': len(assets)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    authority = read_json(args.source_root / 'tools/package/release-preview4-authority.json')
    if args.verify_only:
        result = validate_snapshot(args.source_root, authority)
    elif args.output is None:
        parser.error('--output is required unless --verify-only')
    else:
        result = build(args.source_root, args.output, authority)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
