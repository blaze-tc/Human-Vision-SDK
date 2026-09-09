"""Create an independent Unity import package from compiled native artifacts.

This serializes assets; it does not launch Unity or execute tests.
"""
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
OUTPUT = ROOT / 'out/releases/0.3.0-preview.3'
NAMESPACE = uuid.UUID('9a1f16d6-9fe3-4b94-a3b2-77076251bfec')


def metadata(path):
    guid = uuid.uuid5(NAMESPACE, path).hex
    header = f'fileFormatVersion: 2\nguid: {guid}\n'
    if path.endswith(('.dll', '.so')):
        android = path.endswith('.so')
        platform = ('Android: Android\n    second:\n      enabled: 1\n      settings:\n        CPU: ARM64'
                    if android else 'Standalone: Win64\n    second:\n      enabled: 1\n      settings:\n        CPU: x86_64')
        return header + f'''PluginImporter:
  externalObjects: {{}}
  serializedVersion: 2
  iconMap: {{}}
  executionOrder: {{}}
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


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    assets = {}
    def add(source, path):
        source = Path(source)
        meta = source.with_name(source.name + '.meta')
        assets[path] = (source.read_bytes(), meta.read_bytes() if meta.exists() else metadata(path).encode())
    def text(path, value): assets[path] = (value.encode('utf-8'), metadata(path).encode())
    for section in ('Runtime', 'Demo'):
        for source in (SOURCE / section).rglob('*'):
            if source.is_file() and source.suffix in ('.cs', '.asmdef', '.shader'):
                if source.name in ('HumanVisionDemoBootstrap.cs', 'HumanVisionHud.cs'): continue
                add(source, 'Assets/HumanVision/' + source.relative_to(SOURCE).as_posix())
    # Include editor tools as a unit, without tests or vendor assemblies.
    for source in (SOURCE / 'Editor').glob('*.cs'):
        if source.name not in ('HumanVisionCameraDemoBuilder.cs', 'HumanVisionAndroidBuildSettings.cs'): continue
        add(source, 'Assets/HumanVision/Editor/' + source.name)
    for name in ('humanvision.dll', 'humanvision_onnxruntime.dll', 'hv_dml.dll'):
        add(ROOT / 'build/windows-live/bin/Release' / name, 'Assets/Plugins/x86_64/' + name)
    for name in ('avformat-61.dll', 'avcodec-61.dll', 'avutil-59.dll', 'swscale-8.dll', 'swresample-5.dll'):
        add(ROOT / 'out/live-deps/ffmpeg-windows' / name, 'Assets/Plugins/x86_64/' + name)
    add(ROOT / 'build/android-live/bin/Release/libhumanvision.so', 'Assets/Plugins/Android/arm64-v8a/libhumanvision.so')
    add(ROOT / 'out/live-deps/ort-android/lib/libonnxruntime.so', 'Assets/Plugins/Android/arm64-v8a/libonnxruntime.so')
    for name in ('libavformat.so', 'libavcodec.so', 'libavutil.so', 'libswscale.so', 'libswresample.so'):
        add(ROOT / 'out/live-deps/ffmpeg-android' / name, 'Assets/Plugins/Android/arm64-v8a/' + name)
    text('Assets/Plugins/Android/HumanVisionPermissions.androidlib/AndroidManifest.xml', '''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.humanvision.permissions">
  <uses-permission android:name="android.permission.CAMERA" />
  <uses-permission android:name="android.permission.INTERNET" />
  <uses-feature android:name="android.hardware.camera.any" android:required="false" />
</manifest>
''')
    text('Assets/Plugins/Android/HumanVisionPermissions.androidlib/project.properties', 'android.library=true\ntarget=android-24\n')
    for directory, name in (('detector', 'rtmdet_tiny_person_640.onnx'), ('wholebody', 'rtmpose_s_133.onnx')):
        add(ROOT / 'models' / directory / name, 'Assets/StreamingAssets/HumanVision/Models/' + name)
        add(ROOT / 'models' / directory / ('candidates.json' if directory == 'wholebody' else 'model_info.json'), 'Assets/HumanVision/Documentation/' + directory + '_model_info.json')
    add(ROOT / 'models/detector/rtmdet_tiny_person_640.json', 'Assets/HumanVision/Documentation/person_detector_transform.json')
    add(ROOT / 'docs/SDK_LIVE_CAMERA_GUIDE.md', 'Assets/HumanVision/README.md')
    add(ROOT / 'out/hv-ort-dml/ORT_LICENSE', 'Assets/HumanVision/Licenses/ONNXRuntime.txt')
    add(ROOT / 'out/live-deps/ffmpeg-7.1/COPYING.LGPLv2.1', 'Assets/HumanVision/Licenses/FFmpeg-LGPL-2.1.txt')
    for project in ('mmdetection', 'mmpose'):
        add(ROOT / 'tools/reference/vendor' / project / 'LICENSE', 'Assets/HumanVision/Licenses/' + project + '.txt')
    for name in ('LICENSE.txt', 'LICENSE-CODE.txt', 'ThirdPartyNotices.txt'):
        add(ROOT / 'out/directml-1.15.4' / name, 'Assets/HumanVision/Licenses/DirectML-' + name)
    text('Assets/HumanVision/Licenses/DEPENDENCIES.md', '''# Third-party provenance

ONNX Runtime 1.23.0: https://github.com/microsoft/onnxruntime/tree/v1.23.0
Windows private ORT copy changes DirectML delay import to hv_dml.dll and is unsigned.
DirectML 1.15.4: https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4
FFmpeg 7.1 binaries: ByteDeco JavaCPP presets 1.5.11, non-GPL classifiers.
Build scripts: https://github.com/bytedeco/javacpp-presets/tree/1.5.11/ffmpeg
FFmpeg source: https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
These are dynamically linked replaceable libraries. No FFmpeg JNI library is used.
Model origins/export parameters/hashes are included in Documentation.
OpenMMLab: https://github.com/open-mmlab/mmdetection and https://github.com/open-mmlab/mmpose
This is a user-testing preview, not a claim of model redistribution clearance or hardware acceptance.
''')
    manifest = {path: hashlib.sha256(data).hexdigest() for path, (data, _) in sorted(assets.items())}
    (OUTPUT / 'asset-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    package = OUTPUT / 'HumanVisionSDK-0.3.0-preview.3.unitypackage'
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
    shutil.copy2(ROOT / 'docs/SDK_LIVE_CAMERA_GUIDE.md', OUTPUT / 'README.md')
    shutil.copy2(ROOT / 'docs/SDK_LIVE_CAMERA_PLAN.md', OUTPUT / 'IMPLEMENTATION_PLAN.md')
    archive = OUTPUT / 'HumanVisionSDK-0.3.0-preview.3.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for path in (package, OUTPUT / 'README.md', OUTPUT / 'asset-sha256.json', OUTPUT / 'IMPLEMENTATION_PLAN.md'):
            bundle.write(path, path.name)
        for path in (ROOT / 'native/include/humanvision').glob('*.h'):
            bundle.write(path, 'NativeHeaders/' + path.name)
    (OUTPUT / 'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name for p in (package, archive)) + '\n')
    print(f'Created {len(assets)} assets: {package}\n{archive}\nNo Unity import or runtime tests executed.')

if __name__ == '__main__': main()
