"""Generate the isolated Windows evaluation UPM from pinned real native inputs.

No build, network access, Unity launch or release publication is performed here.
"""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import struct
import tarfile
import uuid

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'upm/com.blazetc.humanvision.pc-demo'
VERSION = '0.4.0-pc.1'
NAMESPACE = uuid.UUID('c468c538-ce2d-46f7-842d-ac4db618c953')
NATIVE = {'humanvision.dll', 'humanvision_onnxruntime.dll', 'hv_dml.dll',
          'avformat-61.dll', 'avcodec-61.dll', 'avutil-59.dll', 'swscale-8.dll', 'swresample-5.dll'}
PLATFORM_DLLS = {'advapi32.dll', 'bcrypt.dll', 'crypt32.dll', 'd3d12.dll',
                'dbghelp.dll', 'dxgi.dll', 'kernel32.dll', 'msvcp140.dll',
                'msvcp140_1.dll', 'msvcrt.dll', 'ole32.dll', 'setupapi.dll',
                'user32.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'ws2_32.dll'}
TEXT = {'.cs', '.asmdef', '.shader', '.json', '.md', '.txt'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode('utf-8')


def canonical(path):
    data = path.read_bytes()
    if path.suffix in TEXT:
        data = data.decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n').encode('utf-8')
    return data


def safe_path(name):
    path = PurePosixPath(name)
    if not name or ':' in name or '\\' in name or path.is_absolute() or '..' in path.parts or path.as_posix() != name:
        raise ValueError('Unsafe package path: ' + name)
    return name


def pe_imports(data):
    """Read ordinary and delay-load DLL imports, rejecting non-AMD64 PE payloads."""
    if data[:2] != b'MZ' or len(data) < 64:
        raise ValueError('Invalid PE image')
    offset = struct.unpack_from('<I', data, 60)[0]
    if data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    optional_size = struct.unpack_from('<H', data, offset + 20)[0]
    optional = offset + 24
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('PE must be AMD64 PE32+')
    sections = []
    for i in range(count):
        start = optional + optional_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, start + 8)
        sections.append((rva, max(virtual_size, raw_size), raw))
    def at(rva):
        for base, size, raw in sections:
            if base <= rva < base + size:
                return raw + rva - base
        raise ValueError('Invalid PE RVA')
    def string(rva):
        start = at(rva)
        end = data.find(b'\0', start)
        if end < 0: raise ValueError('Invalid PE import string')
        return data[start:end].decode('ascii').lower()
    result = set()
    for index, stride, name_offset in ((1, 20, 12), (13, 32, 4)):
        rva, size = struct.unpack_from('<II', data, optional + 112 + index * 8)
        if not rva: continue
        start = at(rva)
        for descriptor in range(start, start + size, stride):
            if not any(data[descriptor:descriptor + stride]): break
            if index == 13 and struct.unpack_from('<I', data, descriptor)[0] != 1:
                raise ValueError('Unsupported PE delay import address mode')
            result.add(string(struct.unpack_from('<I', data, descriptor + name_offset)[0]))
    return result


def native_payload(receipt_path):
    receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if set(receipt['files']) != NATIVE:
        raise ValueError('Native closure must contain exactly the eight Windows libraries')
    if receipt.get('native_load_and_exports') != 'PASS':
        raise ValueError('Native receipt must record verified load and exports')
    if receipt.get('build_flags') != {'HV_USE_DIRECTML': True, 'HV_ENABLE_RTSP': True, 'BUILD_TESTING': False}:
        raise ValueError('Native receipt build flags differ from PC contract')
    if not receipt.get('source_base'):
        raise ValueError('Native receipt source base missing')
    payload, imports = {}, {}
    for name, entry in sorted(receipt['files'].items()):
        safe_path(name)
        source = Path(entry['source'])
        if not source.is_absolute() or not source.is_file():
            raise ValueError('Native source missing or not absolute: ' + name)
        data = source.read_bytes()
        if len(data) != entry['bytes'] or sha(data) != entry['sha256']:
            raise ValueError('Native input hash/byte mismatch: ' + name)
        imports[name] = sorted(pe_imports(data))
        payload['Runtime/Plugins/x86_64/' + name] = data
    # Private ORT/DirectML and FFmpeg imports must resolve in this exact payload.
    for name, dependencies in imports.items():
        for dependency in dependencies:
            if dependency not in NATIVE and dependency not in PLATFORM_DLLS and not dependency.startswith('api-ms-win-'):
                raise ValueError('Unresolved native closure import: ' + name + ' -> ' + dependency)
    return payload, receipt, imports


def metadata(path, folder=False):
    header = 'fileFormatVersion: 2\nguid: ' + uuid.uuid5(NAMESPACE, 'UPM-PC/' + path).hex + '\n'
    if folder:
        return header + 'folderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n'
    if path.endswith('.dll'):
        return header + '''PluginImporter:
  externalObjects: {}
  serializedVersion: 2
  iconMap: {}
  executionOrder: {}
  defineConstraints: []
  isPreloaded: 0
  isOverridable: 0
  isExplicitlyReferenced: 0
  validateReferences: 1
  platformData:
  - first:
      Any:
    second:
      enabled: 0
      settings: {}
  - first:
      Editor: Editor
    second:
      enabled: 1
      settings:
        CPU: x86_64
        OS: Windows
  - first:
      Standalone: Win64
    second:
      enabled: 1
      settings:
        CPU: x86_64
  - first:
      Android: Android
    second:
      enabled: 0
      settings:
        CPU: ARM64
  userData:
  assetBundleName:
  assetBundleVariant:
'''
    suffix = Path(path).suffix
    importer = {'.cs': 'MonoImporter', '.asmdef': 'AssemblyDefinitionImporter', '.shader': 'ShaderImporter'}.get(suffix, 'DefaultImporter')
    extra = '  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n  icon: {instanceID: 0}\n' if suffix == '.cs' else ''
    return header + importer + ':\n  externalObjects: {}\n' + extra + '  userData:\n  assetBundleName:\n  assetBundleVariant:\n'


def build(receipt_path, destination=DEST):
    destination = destination.resolve()
    if destination != DEST.resolve() and not destination.is_relative_to((ROOT / 'out/pc-demo/package-tests').resolve()):
        raise ValueError('Unsafe destination; use isolated PC UPM or package test output')
    payload, receipt, imports = native_payload(Path(receipt_path))
    sources = {}
    def add(source, relative):
        payload[safe_path(relative)] = canonical(source)
        sources[source.relative_to(ROOT).as_posix()] = {'sha256': sha(source.read_bytes()), 'packaged_sha256': sha(payload[relative])}
    unity = ROOT / 'unity/HumanVisionDemo/Assets/HumanVision'
    for section in ('Runtime', 'Demo'):
        for source in sorted((unity / section).rglob('*')):
            if source.is_file() and source.suffix in {'.cs', '.asmdef', '.shader'}:
                if source.name in {'HumanVisionDemoBootstrap.cs', 'HumanVisionHud.cs', 'HumanVisionAndroidGpuGate.cs'}: continue
                relative = source.relative_to(unity).as_posix()
                if section == 'Demo': relative = 'Runtime/' + relative
                add(source, relative)
    add(unity / 'Editor/HumanVisionPcDemoBuilder.cs', 'Editor/HumanVisionPcDemoBuilder.cs')
    add(ROOT / 'tools/package/HumanVisionModelInstaller.cs', 'Editor/HumanVisionModelInstaller.cs')
    payload['Editor/HumanVision.Editor.asmdef'] = encoded({'name': 'HumanVision.Editor', 'references': ['HumanVision.Runtime', 'HumanVision.Demo', 'UnityEngine.UI'], 'includePlatforms': ['Editor'], 'autoReferenced': True})
    runtime = []
    paths = sorted((ROOT / 'modelpacks/rtmo-t-416').iterdir()) + [ROOT / 'profiles' / ('windows-pc-' + backend + '.json') for backend in ('cpu', 'directml')]
    for source in paths:
        if not source.is_file(): raise ValueError('Unexpected model pack directory')
        relative = source.relative_to(ROOT).as_posix()
        add(source, 'RuntimeData/' + relative)
        runtime.append({'path': relative, 'sha256': sha(payload['RuntimeData/' + relative])})
    payload['RuntimeData/index.json'] = encoded({'version': VERSION, 'files': runtime})
    for source in sorted((ROOT / 'upm/com.blazetc.humanvision/Licenses').iterdir()):
        if source.is_file() and source.suffix != '.meta': add(source, 'Licenses/' + source.name)
    add(ROOT / 'docs/PC_DEMO_INSTALLATION.md', 'PC_DEMO_INSTALLATION.md')
    payload['.gitattributes'] = b'* -text\n'
    payload['package.json'] = encoded({'name': 'com.blazetc.humanvision', 'version': VERSION,
        'displayName': 'Human Vision SDK - Windows PC Evaluation Demo', 'unity': '2021.3',
        'description': 'Windows x64 RTMO-t-416 body-only PC evaluation. Explicit DirectML or CPU, capacity 1-8. Exclusive SDK install; no Android or hands.',
        'dependencies': {key: '1.0.0' for key in ('com.unity.ugui', 'com.unity.modules.physics', 'com.unity.modules.imageconversion', 'com.unity.modules.imgui', 'com.unity.modules.jsonserialize', 'com.unity.modules.unitywebrequest', 'com.unity.modules.video')},
        'author': {'name': 'blaze-tc'}, 'repository': {'type': 'git', 'url': 'https://github.com/blaze-tc/Human-Vision-SDK.git'}})
    payload['PROVENANCE.json'] = encoded({'version': VERSION, 'native_source_base': receipt['source_base'],
        'native_build_flags': receipt['build_flags'], 'native_load_and_exports': receipt['native_load_and_exports'],
        'native': {name: {'sha256': entry['sha256'], 'bytes': entry['bytes'], 'imports': imports[name]} for name, entry in sorted(receipt['files'].items())},
        'unity_and_data_sources': sources, 'runtime_index_sha256': sha(payload['RuntimeData/index.json']),
        'platform_dependencies': sorted(PLATFORM_DLLS), 'task1_source_commit': '44ebb094',
        'task_contract': 'out/pc-demo/PLAN.md; Task1 PC source + Task2 isolated UPM; Task3 Unity/player/Git acceptance recorded separately',
        'license_scope': 'Upstream notices and model SOURCES are included. No model redistribution clearance or hardware performance acceptance is asserted.'})
    folders = set()
    for path in tuple(payload):
        for parent in PurePosixPath(path).parents:
            if str(parent) != '.': folders.add(str(parent))
        payload[path + '.meta'] = metadata(path).encode('utf-8')
    for path in folders: payload[path + '.meta'] = metadata(path, True).encode('utf-8')
    payload['asset-sha256.json.meta'] = metadata('asset-sha256.json').encode('utf-8')
    payload['asset-sha256.json'] = encoded({path: sha(data) for path, data in sorted(payload.items())})
    # Reconcile only previous generated assets. Refuse symlinks and unknown files;
    # never recursively delete a caller-controlled directory.
    previous = {}
    if destination.exists():
        index = destination / 'asset-sha256.json'
        if not index.exists(): raise ValueError('Existing destination is not a generated PC package')
        previous = json.loads(index.read_text(encoding='utf-8'))
        known = set(previous) | {'asset-sha256.json'}
        for item in destination.rglob('*'):
            if item.is_symlink(): raise ValueError('Symlink in package destination')
            if item.is_file() and item.relative_to(destination).as_posix() not in known:
                raise ValueError('Unowned file in package destination: ' + str(item))
        for name in previous: safe_path(name)
    destination.mkdir(parents=True, exist_ok=True)
    for relative, data in sorted(payload.items()):
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for relative in sorted(set(previous) - set(payload)):
        path = destination / relative
        if path.is_file(): path.unlink()
    validate_package(destination)
    return destination


def validate_package(destination):
    manifest = json.loads((destination / 'asset-sha256.json').read_text(encoding='utf-8'))
    guids = set()
    for path in sorted(destination.rglob('*')):
        if path.is_symlink(): raise ValueError('Symlink in package')
        if not path.is_file(): continue
        relative = path.relative_to(destination).as_posix()
        safe_path(relative)
        if relative == 'asset-sha256.json': continue
        if relative not in manifest: raise ValueError('Unindexed package file: ' + relative)
        if relative.endswith('.meta'):
            guid = next((line[6:] for line in path.read_text().splitlines() if line.startswith('guid: ')), '')
            if not guid or guid in guids: raise ValueError('Missing or duplicate GUID: ' + relative)
            guids.add(guid)
        if sha(path.read_bytes()) != manifest[relative]: raise ValueError('Package hash mismatch: ' + relative)
        if path.suffix == '.dll': pe_imports(path.read_bytes())
    for relative in manifest:
        safe_path(relative)
        if not (destination / relative).is_file(): raise ValueError('Missing manifest file: ' + relative)
    runtime = json.loads((destination / 'RuntimeData/index.json').read_text())
    for entry in runtime['files']:
        safe_path(entry['path'])
        if sha((destination / 'RuntimeData' / entry['path']).read_bytes()) != entry['sha256']:
            raise ValueError('Runtime hash mismatch: ' + entry['path'])
    for path in destination.rglob('*'):
        if not path.name.endswith('.meta') and path.name != '.gitattributes' and not path.with_name(path.name + '.meta').is_file():
            raise ValueError('Missing asset/folder meta: ' + str(path))
    return manifest


def archive(destination, output):
    output = output.resolve()
    if not output.is_relative_to((ROOT / 'out/pc-demo').resolve()): raise ValueError('Archive destination must be out/pc-demo')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as zipped, tarfile.open(fileobj=zipped, mode='w', format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(destination.rglob('*')):
            if not path.is_file(): continue
            data = path.read_bytes()
            member = tarfile.TarInfo('package/' + path.relative_to(destination).as_posix())
            member.mode = 0o644; member.size = len(data); member.mtime = 0
            tar.addfile(member, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-inputs', required=True, type=Path)
    parser.add_argument('--tgz', type=Path)
    args = parser.parse_args()
    build(args.native_inputs)
    if args.tgz: archive(DEST, args.tgz)
    print('PC package verified:', DEST)
    print('Assets:', len(validate_package(DEST)), 'Manifest SHA256:', sha((DEST / 'asset-sha256.json').read_bytes()))


if __name__ == '__main__': main()
