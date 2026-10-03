"""Audit the independently installable input package and its runtime payload."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def check(root):
    errors = []
    manifest = json.loads((root / 'package.json').read_text(encoding='utf-8-sig'))
    for name in manifest.get('dependencies', {}):
        if name == 'com.blazetc.humanvision' or any(x in name.lower() for x in ('onnx', 'ncnn')):
            errors.append('Inference dependency: ' + name)
    for path in root.rglob('*'):
        if not path.is_file():
            continue
        if path.suffix in ('.onnx', '.param', '.bin'):
            errors.append('Model payload: ' + str(path))
        if path.suffix == '.asmdef':
            for ref in json.loads(path.read_text(encoding='utf-8-sig')).get('references', []):
                if ref in ('HumanVision.Runtime', 'HumanVision.Demo'):
                    errors.append('Inference assembly reference: ' + ref)
    for name in ('Samples~/InputPreview/InputPreview.unity', 'Samples~/InputPreview/InputPreviewController.cs',
                 'Editor/InputPreviewSceneBuilder.cs', 'Documentation~/INPUT_GUIDE.md'):
        if not (root / name).is_file():
            errors.append('Missing independent preview deliverable: ' + name)
    guids = {}
    for meta in root.rglob('*.meta'):
        match = re.search(r'^guid: ([0-9a-f]{32})$', meta.read_text(), re.MULTILINE)
        if not match:
            errors.append('Invalid metadata GUID: ' + str(meta))
        elif match[1] in guids:
            errors.append('Duplicate metadata GUID: ' + str(meta))
        else:
            guids[match[1]] = meta
    payload_path = root / 'native-payload.json'
    if not payload_path.is_file():
        errors.append('Missing native payload provenance manifest')
    else:
        import hashlib
        payload = json.loads(payload_path.read_text())
        for relative, expected in payload['files'].items():
            path = root / relative
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                errors.append('Native payload hash mismatch: ' + relative)
    reader = Path('D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readobj.exe')
    for relative in ('Runtime/Plugins/x86_64/humanvision_input.dll', 'Runtime/Plugins/Android/arm64-v8a/libhumanvision_input.so'):
        path = root / relative
        if not path.is_file():
            errors.append('Missing standalone native input plugin: ' + relative)
            continue
        if path.suffix == '.so' and 'isPreloaded: 1' not in Path(str(path) + '.meta').read_text():
            errors.append('Android input plugin must preload before Vulkan initialization: ' + relative)
        if not reader.is_file():
            errors.append('Pinned native dependency audit tool is unavailable: ' + str(reader))
            continue
        result = subprocess.run([str(reader), '--coff-imports' if path.suffix == '.dll' else '--needed-libs', str(path)], capture_output=True, text=True)
        if result.returncode:
            errors.append('Native dependency audit failed: ' + relative)
        elif re.search(r'onnxruntime|ncnn|libhumanvision\.so|\bhumanvision\.dll|runtime_host', result.stdout, re.IGNORECASE):
            errors.append('Native input plugin depends on inference: ' + relative)
    return errors


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    failures = check(args.root)
    for failure in failures:
        print('FAIL:', failure)
    if failures:
        raise SystemExit(1)
    print('InputPackageHasNoInferenceDependencies PASS')
