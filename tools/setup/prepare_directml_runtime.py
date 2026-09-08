"""Prepare pinned, privately isolated Windows GPU runtime under out/."""
import hashlib
import json
import shutil
import urllib.request
import zipfile
from pathlib import Path

from isolate_directml import isolate


ROOT = Path(__file__).resolve().parents[2]
PACKAGES = (
    ('microsoft.ml.onnxruntime.directml', '1.23.0', 'onnxruntime-directml-1.23.0',
     'a33ec2382b3c440bab74042a135733bb6e5085f293b908d3997688a58fe307e7'),
    ('microsoft.ai.directml', '1.15.4', 'directml-1.15.4',
     '4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9'),
)


def main():
    out = ROOT / 'out'
    out.mkdir(exist_ok=True)
    for package, version, folder, digest in PACKAGES:
        archive = out / (folder + '.zip')
        url = f'https://api.nuget.org/v3-flatcontainer/{package}/{version}/{package}.{version}.nupkg'
        if not archive.exists():
            urllib.request.urlretrieve(url, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Package hash mismatch: {archive}')
        destination = (out / folder).resolve()
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                if not (destination / member.filename).resolve().is_relative_to(destination):
                    raise ValueError('Unsafe archive member')
            bundle.extractall(destination)
    runtime = out / 'hv-ort-dml'
    (runtime / 'include').mkdir(parents=True, exist_ok=True)
    (runtime / 'lib').mkdir(exist_ok=True)
    ort = out / PACKAGES[0][2]
    for path in (ort / 'build/native/include').glob('*.h'):
        shutil.copy2(path, runtime / 'include' / path.name)
    source = ort / 'runtimes/win-x64/native'
    shutil.copy2(source / 'onnxruntime.lib', runtime / 'lib/onnxruntime.lib')
    (runtime / 'lib/onnxruntime.dll').write_bytes(isolate((source / 'onnxruntime.dll').read_bytes()))
    shutil.copy2(ort / 'LICENSE', runtime / 'ORT_LICENSE')
    (runtime / 'provenance.json').write_text(json.dumps({
        'packages': PACKAGES,
        'private_runtime_sha256': hashlib.sha256((runtime / 'lib/onnxruntime.dll').read_bytes()).hexdigest(),
        'modification': 'DirectML delay-import renamed to hv_dml.dll; modified copy is unsigned',
    }, indent=2) + '\n')
    print(runtime)


if __name__ == '__main__':
    main()
