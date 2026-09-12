"""Verify release archives, hashes, GUIDs, semantic data and native isolation."""
import hashlib
import json
import re
import subprocess
import tarfile
from pathlib import Path
from package_live_sdk import ROOT, OUTPUT, SOURCE, VERSION

def sha(data): return hashlib.sha256(data).hexdigest()
def guid(data):
    match = re.search(rb'^guid: ([0-9a-f]{32})\r?$', data, re.M)
    if not match: raise ValueError('Missing asset GUID')
    return match[1].decode()

def main():
    upm = ROOT/'upm/com.blazetc.humanvision'
    package = OUTPUT/('HumanVisionSDK-'+VERSION+'.unitypackage')
    raw = package.read_bytes()
    assert raw[:2] == b'\x1f\x8b' and raw[3] & 8 and raw[10:].split(b'\0',1)[0] == b'archtemp.tar', 'Unity gzip import contract'
    groups = {}; directories = set()
    with tarfile.open(package, 'r:gz') as archive:
        for member in archive:
            if member.isdir(): directories.add(member.name.rstrip('/')); continue
            identity, leaf = member.name.split('/',1)
            assert re.fullmatch('[0-9a-f]{32}',identity) and leaf in ('asset','asset.meta','pathname'), 'Invalid Unity member'
            assert leaf not in groups.setdefault(identity, {}), 'Duplicate Unity member'
            groups[identity][leaf] = archive.extractfile(member).read()
    expected = json.loads((OUTPUT/'asset-sha256.json').read_text())
    actual = {}; seen_guids = set()
    for identity, item in groups.items():
        assert identity in directories and set(item) == {'asset','asset.meta','pathname'}, 'Incomplete Unity GUID group'
        assert guid(item['asset.meta']) == identity and identity not in seen_guids, 'Unity GUID mismatch'
        seen_guids.add(identity); path = item['pathname'].decode(); assert path not in actual
        actual[path] = sha(item['asset'])
        if path.startswith('Assets/HumanVision/'):
            relative = path[len('Assets/HumanVision/'):]
            source = SOURCE/relative
            if source.is_file() and source.suffix in ('.cs','.asmdef','.shader'):
                assert source.read_bytes() == item['asset'], 'Stale source: '+relative
                if source.with_name(source.name+'.meta').exists(): assert guid(source.with_name(source.name+'.meta').read_bytes()) == identity
            if relative.startswith('Demo/'): relative = 'Runtime/'+relative
        elif path.startswith('Assets/Plugins/'): relative = 'Runtime/Plugins/'+path[len('Assets/Plugins/'):]
        elif path.startswith('Assets/StreamingAssets/HumanVision/Runtime/'):
            relative = 'RuntimeData/'+path[len('Assets/StreamingAssets/HumanVision/Runtime/'):]
            assert guid((upm/(relative+'.meta')).read_bytes()) != identity, 'UPM data GUID collides with StreamingAssets'
        else: raise AssertionError('Unexpected release asset: '+path)
        assert (upm/relative).is_file() and sha((upm/relative).read_bytes()) == actual[path], 'UPM/Unity content mismatch: '+relative
        if not relative.startswith('RuntimeData/'): assert guid((upm/(relative+'.meta')).read_bytes()) == identity, 'Script/plugin GUID changed'
    assert actual == expected, 'Release asset manifest mismatch'
    manifest = json.loads((upm/'asset-sha256.json').read_text())
    for path, digest in manifest.items(): assert sha((upm/path).read_bytes()) == digest, 'UPM hash mismatch: '+path
    identities = {}
    for meta in upm.rglob('*.meta'):
        identity = guid(meta.read_bytes()); assert identity not in identities, f'Duplicate UPM GUID: {meta} / {identities.get(identity)}'; identities[identity]=meta
    for file in upm.rglob('*'):
        if file.is_file() and not file.name.endswith('.meta') and file.name != 'asset-sha256.json':
            assert file.relative_to(upm).as_posix() in manifest, 'Unmanifested/stale UPM file: '+str(file)
    index = json.loads((upm/'RuntimeData/index.json').read_text())
    assert index['version'] == VERSION
    for item in index['files']: assert sha((upm/'RuntimeData'/item['path']).read_bytes()) == item['sha256'], 'Runtime index mismatch'
    assert len(list((upm/'RuntimeData/modelpacks').glob('*/manifest.json'))) == 3
    for native in (upm/'Runtime/Plugins').rglob('*'):
        if native.suffix not in ('.dll','.so'): continue
        data=native.read_bytes(); meta=native.with_name(native.name+'.meta').read_text()
        assert 'Any:' in meta and 'enabled: 0' in meta, 'Native platform metadata missing'
        if native.suffix == '.so': assert data[:4] == b'\x7fELF' and data[4] == 2 and int.from_bytes(data[18:20],'little') == 183, 'Expected ARM64 ELF'
        else: assert data[:2] == b'MZ', 'Expected Windows PE'
    descriptor=json.loads((upm/'package.json').read_text());assert descriptor['version']==VERSION
    with tarfile.open(OUTPUT/('com.blazetc.humanvision-'+VERSION+'.tgz'),'r:gz') as archive:
        expected = {'package/'+file.relative_to(upm).as_posix() for file in upm.rglob('*') if file.is_file()}
        assert {member.name for member in archive.getmembers() if member.isfile()} == expected, 'UPM archive incomplete'
        for member in archive:
            if member.isfile():
                relative=member.name.removeprefix('package/');assert member.name.startswith('package/')
                assert archive.extractfile(member).read()==(upm/relative).read_bytes(), 'Local UPM archive mismatch'
    # Exact built library must be shipped, including the additive runtime exports.
    assert (upm/'Runtime/Plugins/x86_64/humanvision.dll').read_bytes()==(ROOT/'build/windows-live/bin/Release/humanvision.dll').read_bytes()
    assert (upm/'Runtime/Plugins/Android/arm64-v8a/libhumanvision.so').read_bytes()==(ROOT/'build/android-live/bin/Release/libhumanvision.so').read_bytes()
    for symbol in (b'HV_Create',b'HV_SubmitFrame',b'HV_RuntimeCreate',b'HV_RuntimeCopy',b'HV_RuntimeGetDiagnostics'):
        assert symbol+b'\0' in (upm/'Runtime/Plugins/x86_64/humanvision.dll').read_bytes(), 'Missing native export '+str(symbol)
    print(f'Package isolation: PASS ({len(actual)} Unity assets, {len(manifest)} UPM files, {len(identities)} GUIDs)')

if __name__ == '__main__': main()
