"""Prepare pinned CPU Android ORT and non-GPL FFmpeg shared libraries for SDK packaging."""
import hashlib
import shutil
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out'
PACKAGES = {
    'ffmpeg-android.jar': ('https://repo.maven.apache.org/maven2/org/bytedeco/ffmpeg/7.1-1.5.11/ffmpeg-7.1-1.5.11-android-arm64.jar', 'a292d7ac25eda14b1a2b0b2b9cadf3e0627e02b0a1f859c1e5f733aac735808c'),
    'ffmpeg-windows.jar': ('https://repo.maven.apache.org/maven2/org/bytedeco/ffmpeg/7.1-1.5.11/ffmpeg-7.1-1.5.11-windows-x86_64.jar', '63205da750f3fbb7fa0e5d23b9e00fc1257e478c33d02dcee9299268309d594b'),
    'ort-android.aar': ('https://repo.maven.apache.org/maven2/com/microsoft/onnxruntime/onnxruntime-android/1.23.0/onnxruntime-android-1.23.0.aar', '2b7e4ed3c3028a1b2afac8dc324442c70b8983fc1a7cb6adda4133030d53f20a'),
    'live-deps/ffmpeg-7.1.tar.xz': ('https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz', '40973d44970dbc83ef302b0609f2e74982be2d85916dd2ee7472d30678a7abe6'),
}


def main():
    for relative, (url, digest) in PACKAGES.items():
        path = OUT / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists(): urllib.request.urlretrieve(url, path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest: raise ValueError(f'Hash mismatch: {relative}')
    headers = OUT / 'live-deps/ffmpeg-headers'
    headers.mkdir(exist_ok=True)
    with tarfile.open(OUT / 'live-deps/ffmpeg-7.1.tar.xz') as tar:
        for entry in tar.getmembers():
            parts = Path(entry.name).parts
            if len(parts) == 3 and parts[1].startswith('lib') and parts[2].endswith('.h') and entry.isfile():
                target = headers / parts[1] / parts[2]
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(tar.extractfile(entry).read())
    (headers / 'libavutil/avconfig.h').write_text('#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n#endif\n')
    for platform in ('windows', 'android'):
        target = OUT / ('live-deps/ffmpeg-' + platform)
        target.mkdir(exist_ok=True)
        with zipfile.ZipFile(OUT / ('ffmpeg-' + platform + '.jar')) as bundle:
            for name in bundle.namelist():
                leaf = Path(name).name
                if (leaf.endswith('.dll') or leaf.endswith('.so')) and 'jni' not in leaf:
                    (target / leaf).write_bytes(bundle.read(name))
                if 'LICENSE' in leaf and not name.endswith('/'):
                    (target / leaf).write_bytes(bundle.read(name))
    target = OUT / 'live-deps/ort-android'
    (target / 'include').mkdir(parents=True, exist_ok=True)
    (target / 'lib').mkdir(exist_ok=True)
    with zipfile.ZipFile(OUT / 'ort-android.aar') as bundle:
        for name in bundle.namelist():
            if name.startswith('headers/') and name.endswith('.h'):
                (target / 'include' / Path(name).name).write_bytes(bundle.read(name))
            if name == 'jni/arm64-v8a/libonnxruntime.so':
                (target / 'lib/libonnxruntime.so').write_bytes(bundle.read(name))
    print('Prepared build dependencies; no inference or runtime tests executed.')

if __name__ == '__main__': main()
