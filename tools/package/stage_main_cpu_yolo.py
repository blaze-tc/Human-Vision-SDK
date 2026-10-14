"""Stage a bounded local Android CPU composition from the restored preview.7 bytes.

The original 47-file GPU/NPU closure is an immutable authority. Only the Android
CPU profile and five new CPU ModelPack files may differ. Nothing is installed in
Unity, publicly uploaded, or marked licensed/device-performance qualified.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re

from package_local_android_addon import read_verified_runtime
from package_release_snapshot import safe_relative

ROOT = Path(__file__).resolve().parents[2]
BASELINE_INDEX_SHA256 = '9fb34467e98658d78561687b44feb836da0b56de23dbee25f16a508a3c98c94c'
SOURCE_RECEIPT_SHA256 = '757d2d329c56dc38ee9b7c0137c07a3f3538b3ec54db750583739293ce7c1fb2'
BASELINE_SDK_SHA256 = '8689efb872d76a825595ec4701375cf0c3c0f44d27a12d9bf2a8c9240ece4681'
INPUT_SHA256 = '0639a50ecbe765bda84a4dbc6616bd9f5fcedbf51839e2dfb7c1588650511457'
WINDOWS_SHA256 = '8a7f5248bf45f24f7f282d4dfbe4b73ceb26752ebf811f74f6afe113b10868c9'
ORT_SHA256 = 'cd1285f8955f3abcb0127d1ffaf1e5da7893d2547872a831b4717c0bfe388328'
VENDOR_SHA256 = '07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351'
MODEL_SHA256 = '6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3'
PACK = 'yolov8n-pose-512x288-ort'
PACK_PATH = 'modelpacks/' + PACK
PROFILE_PATH = 'profiles/android-cpu-nohands.json'
COMPOSITION_ID = 'main-preview7-restored-android-cpu-yolo-v1'
STAGE_RECEIPT = 'cpu-yolo-stage.json'
PACK_FILES = ('manifest.json', 'component.json', 'README.md', 'SOURCES.md', 'body.onnx')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')


def read_file(path):
    try:
        return Path(path).read_bytes()
    except FileNotFoundError as error:
        raise ValueError('Missing required file: ' + str(path)) from error


def read_json(data, label):
    try:
        result = json.loads(data)
    except (ValueError, UnicodeError) as error:
        raise ValueError('Invalid ' + label + ' JSON') from error
    if not isinstance(result, dict):
        raise ValueError('Invalid ' + label + ' object')
    return result


def validate_authority(index_data, seal_data, source_receipt_data):
    """Use pinned recovery evidence, never an arbitrary self-authored index/seal."""
    seal = read_json(seal_data, 'stage seal')
    if (seal.get('schema_version') != 1 or seal.get('stage_id') != 'local-qualified-runtime' or
            seal.get('local_evaluation_only') is not True or seal.get('index_sha256') != digest(index_data)):
        raise ValueError('Original stage receipt/seal mismatch')
    if digest(index_data) != BASELINE_INDEX_SHA256:
        raise ValueError('Original baseline index SHA mismatch')
    if digest(source_receipt_data) != SOURCE_RECEIPT_SHA256:
        raise ValueError('Original source receipt SHA mismatch')
    index = read_json(index_data, 'baseline index')
    source = read_json(source_receipt_data, 'source receipt')
    if (source.get('main_commit') != 'ba1815abe8fa07a134fda8c308a8d1312b5a05fd' or
            source.get('sdk') != '0.4.0-preview.7' or source.get('input') != '0.1.0-preview.5' or
            source.get('runtime_files') != 47):
        raise ValueError('Original source receipt identity mismatch')
    rows = index.get('files')
    if not isinstance(rows, list) or len(rows) != 47:
        raise ValueError('Original source receipt requires the complete 47-file baseline')
    expected = {'runtime/index.json': digest(index_data), 'runtime/staged-runtime.json': digest(seal_data)}
    names = set()
    for row in rows:
        name = row.get('path') if isinstance(row, dict) else None
        if not safe_relative(name) or any(c in name for c in ('\x00', '<', '>', '|', '?', '*')):
            raise ValueError('Unsafe baseline runtime path')
        if name.casefold() in names or name in ('index.json', 'staged-runtime.json'):
            raise ValueError('Duplicate baseline runtime path')
        if not re.fullmatch('[a-f0-9]{64}', row.get('sha256', '')):
            raise ValueError('Invalid baseline runtime hash')
        names.add(name.casefold())
        expected['runtime/' + name] = row['sha256']
    source_files = source.get('files', {})
    actual = {name: value for name, value in source_files.items() if name.startswith('runtime/')}
    if actual != expected:
        raise ValueError('Original source receipt runtime file/hash closure differs')
    binding = index.get('binding_correction', {})
    for key, value in (('sdk_native_sha256', BASELINE_SDK_SHA256),
                       ('input_native_sha256', INPUT_SHA256), ('windows_native_sha256', WINDOWS_SHA256)):
        if binding.get(key) != value:
            raise ValueError('Original source receipt native binding mismatch: ' + key)
    if index.get('npu_experimental', {}).get('runtime_sha256') != VENDOR_SHA256:
        raise ValueError('Original source receipt RKNN runtime binding mismatch')
    if PROFILE_PATH not in {row['path'] for row in rows}:
        raise ValueError('Missing original Android CPU profile')
    return index


def read_baseline(root, source_receipt):
    root = Path(root).resolve()
    index_data = read_file(root/'index.json')
    seal_data = read_file(root/'staged-runtime.json')
    source_data = read_file(source_receipt)
    index = validate_authority(index_data, seal_data, source_data)
    try:
        files = read_verified_runtime(root)
    except FileNotFoundError as error:
        raise ValueError('Missing indexed baseline file: ' + str(error.filename)) from error
    return files, index, source_data


def validate_cpu_contract(profile_data, manifest_data, component_data):
    profile = read_json(profile_data, 'CPU profile')
    expected = {
        'schema_version': 1, 'profile': 'android-cpu-nohands',
        'body': {'pipeline': 'pipeline.yolo.cpu', 'modelPack': PACK},
        'hands': {'enabled': False, 'fps': 15}, 'body_fps': 30, 'output': {'hz': 60},
        'backend': {'preference': ['backend.ort.cpu'], 'allow_fallback': False},
    }
    if profile != expected:
        raise ValueError('CPU profile must use one YOLO ModelPack and backend.ort.cpu exclusively, without fallback')
    manifest = read_json(manifest_data, 'CPU ModelPack')
    for key, value in (('schema_version', 1), ('pack_id', PACK), ('pipeline_id', 'pipeline.yolo.cpu'),
                       ('max_people', 8), ('capabilities', ['body_pose', 'multi_person']),
                       ('local_evaluation_only', True), ('distribution_qualified', False),
                       ('hardware_fps_acceptance', False)):
        if manifest.get(key) != value:
            raise ValueError('CPU ModelPack contract mismatch: ' + key)
    models = manifest.get('models')
    if not isinstance(models, list) or len(models) != 1 or not isinstance(models[0], dict):
        raise ValueError('CPU ModelPack requires exactly one body model')
    model = models[0]
    for key, value in (('role', 'body'), ('format', 'onnx'), ('asset_path', 'body.onnx'),
                       ('sha256', MODEL_SHA256), ('decoder_id', 'yolov8_pose_dfl17_v1')):
        if model.get(key) != value:
            raise ValueError('CPU model asset/decoder contract mismatch: ' + key)
    input_contract = {
        'width': 512, 'height': 288, 'tensor_layout': 'NCHW', 'tensor_dtype': 'fp32',
        'color_order': 'RGB', 'normalization': {'mean': [0, 0, 0], 'norm': [1/255]*3},
        'input_blob': 'in0', 'crop_mode': 'letterbox', 'pad_rgb': [114]*3,
    }
    output_contract = {
        'decoder': 'yolov8_pose_dfl17_v1', 'output_blobs': ['out0', 'out1'],
        'tensor_dtype': 'fp32', 'rows': 3024, 'columns': [65, 51],
        'max_output_bytes': {'out0': 786240, 'out1': 616896},
    }
    if model.get('input_contract') != input_contract or model.get('output_contract') != output_contract:
        raise ValueError('CPU model tensor/letterbox contract mismatch')
    if model.get('backend_options', {}) != {}:
        raise ValueError('CPU model backend overrides are not permitted')
    if not model.get('source') or not model.get('license'):
        raise ValueError('CPU ModelPack source/license provenance is required')
    component = read_json(component_data, 'CPU component')
    for key, value in (('id', 'modelpack.' + PACK), ('type', 'modelpack'), ('api_version', 1),
                       ('capabilities', ['body_pose', 'multi_person']), ('dependencies', ['pipeline.yolo.cpu'])):
        if component.get(key) != value:
            raise ValueError('CPU component contract mismatch: ' + key)
    if not component.get('version') or not component.get('test_targets'):
        raise ValueError('CPU component version/tests are required')


def validate_native(data, expected):
    if not re.fullmatch('[a-f0-9]{64}', expected or '') or digest(data) != expected:
        raise ValueError('New SDK native SHA/hash mismatch')
    if expected == BASELINE_SDK_SHA256:
        raise ValueError('The original SDK binary cannot execute the new CPU pipeline')
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01' or data[16:20] != b'\x03\x00\xb7\x00':
        raise ValueError('New SDK native must be an Android ARM64 shared ELF library')


def extended_index(baseline_index, files, native_sha):
    index = copy.deepcopy(baseline_index)
    index['files'] = [{'path': name, 'sha256': digest(data)} for name, data in sorted(files.items())
                      if name not in ('index.json', 'staged-runtime.json')]
    binding = index['binding_correction']
    binding.update(composition_id=COMPOSITION_ID, sdk_native_sha256=native_sha,
                   distribution_qualified=False, hardware_fps_acceptance=False)
    index['cpu_yolo'] = {
        'schema_version': 1, 'profile': 'android-cpu-nohands', 'pipeline': 'pipeline.yolo.cpu',
        'model_pack': PACK, 'model_sha256': MODEL_SHA256, 'backend': 'backend.ort.cpu',
        'source_index_sha256': BASELINE_INDEX_SHA256, 'hands_enabled': False,
        'body_fps': 30, 'output_hz': 60, 'local_evaluation_only': True,
        'distribution_qualified': False, 'hardware_fps_acceptance': False,
    }
    return index


def stage(baseline_runtime, source_receipt, model, sdk_native, sdk_native_sha256, output,
          *, source_root=None):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError('Use a fresh new output directory')
    source_root = Path(source_root or ROOT).resolve()
    files, baseline_index, source_data = read_baseline(baseline_runtime, source_receipt)
    native_data = read_file(sdk_native)
    validate_native(native_data, sdk_native_sha256)
    model_data = read_file(model)
    if digest(model_data) != MODEL_SHA256:
        raise ValueError('Pinned CPU model SHA/hash mismatch')
    templates = {}
    for name in (PROFILE_PATH,) + tuple(PACK_PATH + '/' + name for name in PACK_FILES[:-1]):
        path = source_root/name
        if not path.resolve().is_relative_to(source_root):
            raise ValueError('Linked CPU template path escapes the source root')
        templates[name] = read_file(path)
    validate_cpu_contract(templates[PROFILE_PATH], templates[PACK_PATH+'/manifest.json'],
                          templates[PACK_PATH+'/component.json'])
    templates[PACK_PATH+'/body.onnx'] = model_data
    original = {name: data for name, data in files.items() if name not in ('index.json', 'staged-runtime.json')}
    composed = dict(original)
    composed.update(templates)
    index = extended_index(baseline_index, composed, sdk_native_sha256)
    index_data = json_bytes(index)
    seal_data = json_bytes({'schema_version': 1, 'stage_id': 'local-qualified-runtime',
                            'local_evaluation_only': True, 'index_sha256': digest(index_data)})
    preserved = {name: digest(data) for name, data in original.items() if name != PROFILE_PATH}
    receipt = {
        'schema_version': 1, 'composition_id': COMPOSITION_ID,
        'source_index_sha256': BASELINE_INDEX_SHA256, 'source_receipt_sha256': digest(source_data),
        'index_sha256': digest(index_data), 'runtime_file_count': len(index['files']),
        'sdk_native_sha256': sdk_native_sha256, 'input_native_sha256': INPUT_SHA256,
        'windows_native_sha256': WINDOWS_SHA256, 'onnxruntime_sha256': ORT_SHA256,
        'rknn_runtime_sha256': VENDOR_SHA256, 'model_sha256': MODEL_SHA256,
        'preserved_files': preserved,
        'changed_files': {PROFILE_PATH: {'before': digest(original[PROFILE_PATH]),
                                        'after': digest(templates[PROFILE_PATH])}},
        'added_files': {name: digest(data) for name, data in templates.items() if name != PROFILE_PATH},
        'local_evaluation_only': True, 'distribution_qualified': False,
        'hardware_fps_acceptance': False, 'public_upload_allowed': False,
    }
    # Validate everything above before creating any output. Existing outputs are
    # never repaired in place; a failed post-write check remains auditable.
    output.mkdir(parents=True)
    for name, data in {**composed, 'index.json': index_data, 'staged-runtime.json': seal_data}.items():
        target = output/'Runtime'/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    audit_files = {'baseline/index.json': files['index.json'],
                   'baseline/staged-runtime.json': files['staged-runtime.json'],
                   'baseline/source-receipt.json': source_data,
                   'native/libhumanvision.so': native_data,
                   STAGE_RECEIPT: json_bytes(receipt)}
    for name, data in audit_files.items():
        target = output/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    read_stage(output)
    return receipt


def read_stage(root):
    """Verify the exact allowed delta against retained immutable recovery evidence."""
    root = Path(root).resolve()
    receipt = read_json(read_file(root/STAGE_RECEIPT), 'CPU stage receipt')
    baseline = validate_authority(read_file(root/'baseline/index.json'),
                                  read_file(root/'baseline/staged-runtime.json'),
                                  read_file(root/'baseline/source-receipt.json'))
    for key, value in (('schema_version', 1), ('composition_id', COMPOSITION_ID),
                       ('source_index_sha256', BASELINE_INDEX_SHA256),
                       ('source_receipt_sha256', SOURCE_RECEIPT_SHA256),
                       ('local_evaluation_only', True), ('distribution_qualified', False),
                       ('hardware_fps_acceptance', False), ('public_upload_allowed', False),
                       ('input_native_sha256', INPUT_SHA256), ('windows_native_sha256', WINDOWS_SHA256),
                       ('onnxruntime_sha256', ORT_SHA256), ('rknn_runtime_sha256', VENDOR_SHA256),
                       ('model_sha256', MODEL_SHA256)):
        if receipt.get(key) != value:
            raise ValueError('CPU stage receipt mismatch: ' + key)
    runtime_root = root/'Runtime'
    try:
        files = read_verified_runtime(runtime_root)
    except FileNotFoundError as error:
        raise ValueError('Missing indexed CPU stage file: ' + str(error.filename)) from error
    if digest(files['index.json']) != receipt.get('index_sha256'):
        raise ValueError('CPU stage hash/receipt mismatch')
    validate_native(read_file(root/'native/libhumanvision.so'), receipt.get('sdk_native_sha256'))
    index = read_json(files['index.json'], 'CPU stage index')
    original = {row['path']: row['sha256'] for row in baseline['files']}
    expected_names = set(original) | {PACK_PATH+'/'+name for name in PACK_FILES}
    current = {name: digest(data) for name, data in files.items() if name not in ('index.json', 'staged-runtime.json')}
    if set(current) != expected_names or len(current) != 52 or receipt.get('runtime_file_count') != len(current):
        raise ValueError('CPU stage receipt file closure differs')
    preserved = {name: expected for name, expected in original.items() if name != PROFILE_PATH}
    if receipt.get('preserved_files') != preserved or any(current[name] != expected for name, expected in preserved.items()):
        raise ValueError('CPU stage preserved GPU/NPU/model/profile closure differs')
    changes = {PROFILE_PATH: {'before': original[PROFILE_PATH], 'after': current[PROFILE_PATH]}}
    additions = {name: current[name] for name in current if name not in original}
    if receipt.get('changed_files') != changes or receipt.get('added_files') != additions:
        raise ValueError('CPU stage receipt delta differs')
    validate_cpu_contract(files[PROFILE_PATH], files[PACK_PATH+'/manifest.json'], files[PACK_PATH+'/component.json'])
    if digest(files[PACK_PATH+'/body.onnx']) != MODEL_SHA256:
        raise ValueError('Pinned CPU model SHA/hash mismatch')
    if index != extended_index(baseline, files, receipt['sdk_native_sha256']):
        raise ValueError('CPU stage index contains changes beyond the allowed CPU composition')
    return {'files': files, 'index': index, 'receipt': receipt}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline-runtime', 'source-receipt', 'model', 'sdk-native', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--sdk-native-sha256', required=True)
    args = parser.parse_args()
    result = stage(args.baseline_runtime, args.source_receipt, args.model, args.sdk_native,
                   args.sdk_native_sha256, args.output)
    print(json.dumps({'runtime': str(args.output/'Runtime'), 'runtime_files': result['runtime_file_count'],
                      'index_sha256': result['index_sha256'], 'stage_receipt': str(args.output/STAGE_RECEIPT),
                      'hardware_fps_acceptance': False, 'public_upload_allowed': False}, indent=2))


if __name__ == '__main__': main()
