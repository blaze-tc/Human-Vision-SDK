"""Compose a private RK3588 experiment after immutable offline numerical gates.

No vendor/model binary is committed or marked production qualified. The caller
must pass the actual private model and saved receipts, not fabricated success.
Existing Vulkan files are preserved; an index records the exact mixed APK data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
TEMPLATES=ROOT/'tools/models/rknn/templates'
MODEL_SHA='1b3ba8dd5bb81e3d968cfeef019c42b1aa3bffca6f0ec1c9f89f257fe0c08030'
ONNX_SHA='6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3'
BANK_SHA='5ab8bb97034e957f3176162a63b471790b64514c7ba5a34d17622190167d4c34'
RUNTIME_SHA='07a8398be5caed21a1998ffa313e0425bb9e76b154a8dbbc667d71722c009351'
CONVERSION_SHA='244a28f1bc9e0b1665fe27a3b35e774a732f9f62f752b5f6faed376e28bef4bb'
COMPARISON_SHA='fe29f669bd5fafecba7a21797cad35f99b9f21b540c419a5f479d98cd03687f9'
PROFILE='android-rknn-npu-quality-low'
PACK='yolov8n-pose-rectangle512x288-rknn-nonquantized-experimental'

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def json_bytes(value): return (json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
def json_hash(value): return hashlib.sha256(json_bytes(value)).hexdigest()
def write_json(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(json_bytes(value))

def validate_receipts(receipt,simulator):
    expected=dict(target='rk3588',toolkit_version='2.3.2',source_onnx_sha256=ONNX_SHA,
        rknn_sha256=MODEL_SHA,precision_request='non-quantized',deployment_ready=False,
        device_performance_verified=False)
    if any(receipt.get(key)!=value for key,value in expected.items()):
        raise ValueError('Unpinned/nonexperimental conversion receipt; INT8 is not admitted')
    identity=simulator.get('identity',{})
    if (simulator.get('offline_numerical_passed') is not True or
        simulator.get('deployment_ready') is not False or
        simulator.get('device_performance_verified') is not False or
        identity.get('rknn_sha256')!=MODEL_SHA or identity.get('validation_index_sha256')!=BANK_SHA):
        raise ValueError('Missing exact offline numerical gate; not a device qualification')
    fixtures=simulator.get('fixtures',[])
    if len(fixtures)!=3 or sorted(row.get('expected_people',-1) for row in fixtures)!=[0,1,7] or any(row.get('passed') is not True for row in fixtures):
        raise ValueError('All unchanged seven/one/empty numerical controls are required')

def build_manifest(profile,conversion_sha,comparison_sha):
    if conversion_sha!=CONVERSION_SHA or comparison_sha!=COMPARISON_SHA:
        raise ValueError('Private offline receipt SHA mismatch with the native tensor pipeline')
    return dict(schema_version=1,pack_id=PACK,pack_version='0.4.0-preview.6.npu-experimental.1',
        profile_id=PROFILE,profile_sha256=json_hash(profile),pipeline_id='pipeline.yolo.tensor',
        capabilities=['body_pose','multi_person'],max_people=8,
        experimental=True,local_evaluation_only=True,execution_contract='raw_tensor_rknn_nonquantized_v1',
        qualification=dict(offline_numerical_passed=True,deployment_ready=False,
            device_performance_verified=False,validation_index_sha256=BANK_SHA,
            source_onnx_sha256=ONNX_SHA,conversion_receipt_sha256=conversion_sha,
            simulator_comparison_sha256=comparison_sha),
        models=[dict(role='body',format='rknn',decoder_id='yolov8_pose_dfl17_v1',
            asset_path='body.rknn',sha256=MODEL_SHA,
            input_contract=dict(width=512,height=288,color_order='RGB',tensor_dtype='uint8',
                tensor_layout='NHWC',input_blob='in0',crop_mode='letterbox',pad_rgb=[114]*3,
                normalization=dict(mean=[0]*3,norm=[1/255]*3)),
            output_contract=dict(decoder='yolov8_pose_dfl17_v1',output_blobs=['out0','out1'],
                tensor_dtype='fp32',rows=3024,columns=[65,51],max_output_bytes=dict(out0=3024*65*4,out1=3024*51*4)),
            backend_options=dict(runtime_library='librknnrt.so',core_mask=7,input_layout='nhwc',input_type='uint8'),
            source='Pinned NCNN weights -> bounded ONNX recovery -> RKNN-Toolkit2 2.3.2; local experimental parity only',
            license='Original model upstream license and private RKNN runtime distribution gates apply; no public redistribution qualification',
            conversion_recipe='tools/models/rknn/recover_pinned_onnx.py + convert_candidate.py non-quantized',
            evidence=[dict(path='conversion-receipt.json',sha256=conversion_sha),
                      dict(path='simulator-comparison.json',sha256=comparison_sha)])])

def reindex(root,native_sha,input_sha,runtime_sha):
    root=Path(root);index_path=root/'index.json'
    receipt_path=root/'staged-runtime.json'
    index=json.loads(index_path.read_text(encoding='utf-8-sig'))
    index['files']=[dict(path=path.relative_to(root).as_posix(),sha256=sha(path))
        for path in sorted(root.rglob('*')) if path.is_file() and path not in (index_path,receipt_path) and path.suffix!='.meta']
    identity=index.setdefault('binding_correction',{})
    identity.update(sdk_native_sha256=native_sha,input_native_sha256=input_sha,
        distribution_qualified=False,hardware_fps_acceptance=False)
    index['npu_experimental']=dict(runtime_mode='android-dual-vulkan-npu',profile=PROFILE,
        model_sha256=MODEL_SHA,runtime_sha256=runtime_sha,offline_numerical_passed=True,
        deployment_ready=False,device_performance_verified=False)
    write_json(index_path,index)
    # The Editor installer validates this explicit local stage and preserves it
    # across domain reloads/builds. It is a hash receipt, not hardware approval.
    write_json(receipt_path,dict(schema_version=1,stage_id='local-qualified-runtime',
        local_evaluation_only=True,index_sha256=sha(index_path)))

def validate_receipt_files(conversion,comparison):
    # Admission uses immutable actual bytes, not caller-authored success fields.
    # Check before any Runtime mutation and keep the pins equal to the native plugin.
    if sha(conversion)!=CONVERSION_SHA or sha(comparison)!=COMPARISON_SHA:
        raise ValueError('Private offline receipt SHA mismatch with the native tensor pipeline')
    receipt=json.loads(Path(conversion).read_text(encoding='utf-8-sig'))
    simulator=json.loads(Path(comparison).read_text(encoding='utf-8-sig'))
    validate_receipts(receipt,simulator)

def stage(root,model,conversion,comparison,native_sha,input_sha,runtime):
    root=Path(root).resolve();model=Path(model).resolve();runtime=Path(runtime).resolve()
    if not (root/'index.json').is_file(): raise ValueError('Existing indexed Vulkan Runtime root is required')
    if sha(model)!=MODEL_SHA or sha(runtime)!=RUNTIME_SHA: raise ValueError('Private model/runtime SHA mismatch')
    validate_receipt_files(conversion,comparison)
    profile=json.loads((TEMPLATES/(PROFILE+'.json')).read_text())
    write_json(root/'profiles'/(PROFILE+'.json'),profile)
    pack=root/'modelpacks'/PACK;pack.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(model,pack/'body.rknn')
    shutil.copyfile(conversion,pack/'conversion-receipt.json')
    shutil.copyfile(comparison,pack/'simulator-comparison.json')
    manifest=build_manifest(profile,sha(conversion),sha(comparison))
    write_json(pack/'manifest.json',manifest)
    reindex(root,native_sha,input_sha,sha(runtime))
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['runtime-root','model','conversion','comparison','runtime-library','native-sha','input-sha']:
        parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    stage(args.runtime_root,args.model,args.conversion,args.comparison,args.native_sha,args.input_sha,args.runtime_library)
    print('Private experimental Runtime composition: PASS; RK3588 device acceptance remains open')

if __name__=='__main__': main()
