"""Pinned, fixture-only tools. No downloaded binary enters the input package."""
import hashlib, json, pathlib, sys

LOCK = {
    'mediamtx_version': 'v1.12.3',
    'mediamtx_url': 'https://github.com/bluenviron/mediamtx/releases/download/v1.12.3/mediamtx_v1.12.3_windows_amd64.zip',
    'mediamtx_archive_sha256': 'd87b5080e5d2f2061b0ea6f4b64945d88ecf8053021b6a121a05b53adb57d3bb',
    'mediamtx_sha256': '2df4c6393f35d71d9578528f6a70406b0088490f33df2ef721a85b396445c4b8',
    'ffmpeg_version': '8.1.1-essentials_build-www.gyan.dev',
    'ffmpeg_source': 'https://www.gyan.dev/ffmpeg/builds/',
    'ffmpeg_sha256': '228d7a8556258de907fdb55f36850078ebc7680b84ec30d84ea02e99bec1d1eb',
    'video_sha256': 'e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8',
    'native_sha256': '9a3cb1446206f75d4b7102015164d44b5e81d24fcb9efebcac1f56aa0ae5c66f',
}

def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

if __name__ == '__main__':
    if len(sys.argv) == 1:
        print(json.dumps(LOCK))
    else:
        root, output = map(pathlib.Path, sys.argv[1:3])
        paths = list((root/'upm/com.blazetc.humanvision.input').rglob('*'))
        paths += [root/'tools/test/run_rtsp_fixture.ps1', root/'tools/test/rtsp_fixture_manifest.py']
        output.write_text(json.dumps({'lock': LOCK, 'sourceSha256': {str(p): sha(p) for p in paths if p.is_file()}}, indent=2))
        if len(sys.argv) > 3:
            project = pathlib.Path(sys.argv[3])
            manifest = json.loads((project/'Packages/manifest.json').read_text(encoding='utf-8-sig'))
            packages = {}
            for name in ('com.unity.test-framework', 'com.unity.ext.nunit'):
                declared = manifest['dependencies'][name]
                package = pathlib.Path(declared[5:]) if declared.startswith('file:') else project/'Library/PackageCache'/(name+'@'+declared)
                package_json = json.loads((package/'package.json').read_text(encoding='utf-8-sig'))
                packages[name] = {'path': str(package), 'version': package_json['version'],
                                  'packageJsonSha256': sha(package/'package.json'),
                                  'contentSha256': {str(p.relative_to(package)): sha(p) for p in sorted(package.rglob('*')) if p.is_file()}}
            receipt = json.loads(output.read_text())
            receipt['testToolPackages'] = packages
            output.write_text(json.dumps(receipt, indent=2))
