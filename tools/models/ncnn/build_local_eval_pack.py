"""Build only the verified, local-only Revision3 detector + Body26 candidate.

This gate deliberately pins the one accepted graph and four-image evidence.
Changing a model requires new eligibility evidence and a reviewed pin update.
"""
from __future__ import annotations

import argparse
import copy
import json
import shutil
from pathlib import Path

from tools.models.ncnn.compare_pose_outputs import compare_pose, fixture_image_size
from tools.models.ncnn.model_contract import (DETECTOR_CHECKPOINT_SHA256, POSE_CHECKPOINT_SHA256,
    PINNED_REVISIONS, canonical_json, model_input_contract, require_hash, sha256_file,
    pinned_source_url, verify_provenance)

ROOT = Path(__file__).resolve().parents[3]
PACK_ID = 'precision-t-26-ncnn-fp16'
PROFILE_ID = 'android-ncnn-vulkan'
PROFILE_SHA256 = '3aa57d5c12bc5e39d05d0a305d100708ab7e712f5a65f1a48bd82b4f0ed36091'
CAPABILITIES = ['body_pose','multi_person','gpu_input','vulkan','fp16-storage',
                'fp16-arithmetic','android-hardware-buffer','external-sync-fd']
POSE_ONNX_SHA256 = 'cb53464f622e08682a346661a9529d67cd505c58e36f06c967f5ab1ffe550201'
POSE_GOLDEN_SHA256 = '71738a5d47b7a8a30e371dcd941a7ce8eedd86f1036be377a6a86926230b4312'
POSE_PARAM_SHA256 = '2ece391bc5947ddf30e38498977afd9457a61a99eb63393ebd5d38e3708644ec'
POSE_BIN_SHA256 = '0f8a0a864be7af7990366bfb8ce89d4fca911a08b967c00e3b8180725d5054a4'
POSE_RUNNER_SHA256 = '7077b4941cafdeaa998ef55d3c6468c1818d7b7778a4c56811a81dbf0ff6a8b1'
POSE_RUNNER_SOURCE_SHA256 = '659de78ad25cac3bdf8f0919b035687619df52647dfeb746cc041a4c0b8af6f6'
VKMAT_AUDIT = {
    'input_route':'vkmat','input_dims':3,'input_w':192,'input_h':256,'input_c':1,
    'input_pack':4,'input_bits':16,'vulkan_layers':166,'unsupported_layers':0,
    'fp16_packed':True,'fp16_storage':True,'fp16_arithmetic':False,'subgroup':False,
    'output_route':'vkmat-to-fp32-pack1','x_shape':[1,26,384],'y_shape':[1,26,512],
    'x_bytes':39936,'y_bytes':53248}
CONVERTER_SHA256 = 'fc356c134ac02493ef2d4160dcbff09fdd9a8ed2af35067206f51a9f6897f408'
OPTIMIZER_SHA256 = '40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7'
MODEL_HASHES = {
    'detector': {'param':'9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3',
                 'bin':'4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da',
                 'checkpoint':DETECTOR_CHECKPOINT_SHA256},
    'body': {'param':POSE_PARAM_SHA256,'bin':POSE_BIN_SHA256,'checkpoint':POSE_CHECKPOINT_SHA256}}
OPTIONS = {'detector':{'use_subgroup_ops':True,'use_fp16_arithmetic':True},
           'body':{'use_subgroup_ops':False,'use_fp16_arithmetic':False}}
DECODERS = {'detector':'rtmdet_nano_raw_v1','body':'simcc_body26_v1'}
LICENSE = 'OpenMMLab source Apache-2.0; trained-weight and dataset redistribution rights unverified; local evaluation only'


def confined_file(root: Path, relative: str) -> Path:
    if not isinstance(relative,str) or not relative or '\\' in relative or ':' in relative or Path(relative).is_absolute():
        raise ValueError('asset path must be relative inside ModelPack')
    path=(root/relative).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError('asset path escapes ModelPack or is missing')
    return path


def verify_pose_golden(folder: Path) -> dict:
    verify_vkmat_evidence(folder)
    require_hash(folder/'index.json',POSE_GOLDEN_SHA256,'accepted four-image pose golden')
    index=json.loads((folder/'index.json').read_text(encoding='utf-8'))
    if set(index) != {'full-body','clipped-person','mirrored','rotated'}:
        raise ValueError('four-image pose coverage missing')
    for name,item in index.items():
        case=folder/name
        for filename,digest in item['files'].items():
            require_hash(confined_file(case,filename),digest,name+' '+filename)
        reference,candidate,repeat=[json.loads((case/(label+'.json')).read_text(encoding='utf-8')) for label in ('reference','candidate','repeat')]
        metrics=compare_pose(reference,candidate,reference['bbox'],fixture_image_size(case/'image.png'),repeat)
        if metrics != item['metrics']['strict']:
            raise ValueError('recomputed pose metrics differ from golden')
    return index


def parse_vkmat_audit(log: str) -> dict:
    records=[line[len('HV_POSE_AUDIT '):] for line in log.splitlines() if line.startswith('HV_POSE_AUDIT ')]
    if len(records) != 1:
        raise ValueError('VkMat route requires one successful runtime audit')
    audit=json.loads(records[0])
    if canonical_json(audit) != canonical_json(VKMAT_AUDIT):
        raise ValueError('VkMat geometry/options/output contract mismatch')
    return audit


def verify_vkmat_evidence(folder: Path, *, packed: bool = False) -> dict:
    path=folder/('pose-vkmat-route.json' if packed else 'vkmat-route.json')
    if not path.is_file():
        raise ValueError('VkMat input route evidence is missing; Mat-only runs are ineligible')
    evidence=json.loads(path.read_text(encoding='utf-8'))
    expected_model={'onnx':POSE_ONNX_SHA256,'param':POSE_PARAM_SHA256,'bin':POSE_BIN_SHA256,'checkpoint':POSE_CHECKPOINT_SHA256}
    if (evidence.get('schema_version') != 1 or evidence.get('mode') != 'strict-vkmat'
            or evidence.get('runner_sha256') != POSE_RUNNER_SHA256
            or evidence.get('runner_source_sha256') != POSE_RUNNER_SOURCE_SHA256
            or evidence.get('model_sha256') != expected_model
            or evidence.get('golden_sha256') != POSE_GOLDEN_SHA256):
        raise ValueError('VkMat runner/model/options provenance mismatch')
    index_path=folder/('pose-golden-index.json' if packed else 'index.json')
    require_hash(index_path,evidence['golden_sha256'],'VkMat golden')
    index=json.loads(index_path.read_text(encoding='utf-8'))
    runs=evidence.get('runs',[])
    expected={(case,prefix) for case in ('full-body','clipped-person','mirrored','rotated') for prefix in ('ncnn','repeat')}
    if len(runs)!=8 or {(r.get('case'),r.get('prefix')) for r in runs} != expected:
        raise ValueError('VkMat requires all eight unique case executions')
    for run in runs:
        case,prefix=run['case'],run['prefix']
        log_path=f'route-logs/{case}-{prefix}.log'
        require_hash(confined_file(folder,log_path),run['log_sha256'],'VkMat execution log')
        audit=parse_vkmat_audit((folder/log_path).read_text(encoding='utf-8'))
        if canonical_json(run.get('audit')) != canonical_json(audit):
            raise ValueError('VkMat recorded audit differs from runtime log')
        for label,file in (('input',f'{case}/input.fp32'),('x',f'{case}/{prefix}-x.fp32'),('y',f'{case}/{prefix}-y.fp32')):
            if run[label+'_sha256'] != index[case]['files'][Path(file).name]:
                raise ValueError('VkMat execution tensor differs from pinned golden')
            if not packed:
                require_hash(confined_file(folder,file),run[label+'_sha256'],'VkMat '+label)
    return evidence


def verify_pack(root: Path, manifest: dict | None = None) -> dict:
    if manifest is None: manifest=json.loads((root/'modelpack.json').read_text(encoding='utf-8'))
    if (manifest.get('schema_version') != 2 or manifest.get('pack_id') != PACK_ID
            or manifest.get('profile_id') != PROFILE_ID or manifest.get('pipeline_id') != 'pipeline.topdown'
            or manifest.get('local_evaluation_only') is not True or manifest.get('capabilities') != CAPABILITIES):
        raise ValueError('local schema-2 ModelPack identity mismatch')
    if manifest.get('profile_sha256') != PROFILE_SHA256:
        raise ValueError('pinned profile hash mismatch')
    require_hash(ROOT/'profiles'/f'{PROFILE_ID}.json',PROFILE_SHA256,'profile')
    models=manifest.get('models',[])
    if [m.get('role') for m in models] != ['detector','body']:
        raise ValueError('ModelPack must contain detector and body roles')
    for model in models:
        role=model['role']
        if (model.get('format') != 'ncnn' or model.get('decoder_id') != DECODERS[role]
                or model.get('input_contract') != model_input_contract(role)
                or model.get('backend_options') != OPTIONS[role]
                or any(type(v) is not bool for v in model.get('backend_options',{}).values())):
            raise ValueError('pinned model format/input/backend options mismatch')
        for kind,digest in MODEL_HASHES[role].items():
            if model.get(kind+'_sha256') != digest:
                raise ValueError(role+' '+kind+' SHA-256 is not eligible')
            if kind != 'checkpoint': require_hash(confined_file(root,model[kind+'_path']),digest,role+' '+kind)
        output=model.get('output_contract',{})
        expected_shapes={'cls':[1,2100,1],'bbox':[1,2100,4]} if role=='detector' else {'simcc_x':[1,26,384],'simcc_y':[1,26,512]}
        names=list(expected_shapes)
        if output.get('decoder') != DECODERS[role] or output.get('output_blobs') != names:
            raise ValueError('output decoder/blob mismatch')
        for name,shape in expected_shapes.items():
            size=4
            for dim in shape:size*=dim
            if output.get(name) != {'shape':shape,'download_dtype':'fp32','max_bytes':size} or output.get('max_output_bytes',{}).get(name) != size:
                raise ValueError('static output shape/byte bound mismatch')
        if (model.get('source') != pinned_source_url(role) or model.get('license') != LICENSE
                or model.get('source_revisions') != PINNED_REVISIONS or not model.get('conversion_recipe')):
            raise ValueError('missing pinned model source/license/recipe')
    if models[1].get('onnx_sha256') != POSE_ONNX_SHA256:
        raise ValueError('pose ONNX hash mismatch')
    for model in models:
        converter = CONVERTER_SHA256 if model['role']=='body' else 'b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb'
        if model.get('onnx2ncnn_sha256') != converter or model.get('ncnnoptimize_sha256') != OPTIMIZER_SHA256:
            raise ValueError('pinned conversion tool hash mismatch')
    required_evidence={'pose-golden-index.json','pose-vkmat-route.json','detector-regression-index.json','ncnn-provenance.json','OpenMMLab-source-LICENSE','pose-conversion.json','detector-conversion.json'}
    required_evidence.update(f'route-logs/{case}-{prefix}.log' for case in ('full-body','clipped-person','mirrored','rotated') for prefix in ('ncnn','repeat'))
    if {a['path'] for a in manifest.get('evidence',[])} != {'provenance/'+n for n in required_evidence}:
        raise ValueError('pack evidence set mismatch')
    for artifact in manifest['evidence']:
        require_hash(confined_file(root,artifact['path']),artifact['sha256'],'pack evidence')
    require_hash(root/'provenance/pose-golden-index.json',POSE_GOLDEN_SHA256,'accepted pose evidence')
    verify_vkmat_evidence(root/'provenance',packed=True)
    require_hash(root/'provenance/detector-regression-index.json','5ce63a5222ca9debbafb4bdfe70de997117b516fe0b00f96d4521a6e039df3ae','accepted detector evidence')
    require_hash(root/'provenance/OpenMMLab-source-LICENSE','d125421b289cd79bf03e8590858fa81c22dc95e2753b20bf80e6a3cc80a893f9','source license')
    return manifest


def build_pack(detector_dir: Path, pose_dir: Path, output: Path) -> dict:
    verify_pose_golden(pose_dir/'golden')
    for path,digest in ((pose_dir/'model.onnx',POSE_ONNX_SHA256),(pose_dir/'vulkan.param',POSE_PARAM_SHA256),(pose_dir/'model.bin',POSE_BIN_SHA256)):
        require_hash(path,digest,'pose candidate')
    conversion=json.loads((pose_dir/'conversion.json').read_text(encoding='utf-8'))
    if (conversion['param_sha256'] != POSE_PARAM_SHA256 or conversion['bin_sha256'] != POSE_BIN_SHA256
            or conversion['first_norm_fusion']['onnx_sha256'] != POSE_ONNX_SHA256
            or conversion['onnx2ncnn_sha256'] != CONVERTER_SHA256 or conversion['ncnnoptimize_sha256'] != OPTIMIZER_SHA256):
        raise ValueError('pose conversion provenance differs from eligible graph')
    ncnn_pin=json.loads((ROOT/'third_party/ncnn/provenance.json').read_text(encoding='utf-8'))
    if ([p['sha256'] for p in ncnn_pin['patches']] != ['ec127f7b9e3d7c442c5cee9d7cd06d6c43d3d3d87fe7cab8ff21934144e1f140',
           '5bccb88a80393e59366267c54db559e6eeb55fb3a18bc6ad6c7be93b6cda174f']):
        raise ValueError('Model requires the audited AHB and subgroup-option patch chain')
    require_hash(ROOT/'out/c2-vendor/mmpose/LICENSE','d125421b289cd79bf03e8590858fa81c22dc95e2753b20bf80e6a3cc80a893f9','OpenMMLab source license')
    detector=json.loads((detector_dir/'model.json').read_text(encoding='utf-8'))
    if detector.get('local_evaluation_only') is not True:raise ValueError('detector is not local evaluation')
    verify_provenance(detector,{'checkpoint':ROOT/'out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth',
        'onnx':ROOT/'out/c2-detector/rtmdet-nano.onnx','param':detector_dir/'model.param','bin':detector_dir/'model.bin',
        'fixture':ROOT/'out/c2-detector/golden/official/image.png'})
    require_hash(pose_dir/'detector/index.json','5ce63a5222ca9debbafb4bdfe70de997117b516fe0b00f96d4521a6e039df3ae','eight detector output regression')
    for case,tensors in json.loads((pose_dir/'detector/index.json').read_text(encoding='utf-8')).items():
        for name,item in tensors.items():
            if item['matches_golden'] is not True:raise ValueError('detector regression failed')
            require_hash(pose_dir/'detector'/f'{case}-{name}.fp32',item['sha256'],'detector tensor')
    audit=(pose_dir/'four-case.log').read_text(encoding='utf-8')
    if audit.count('Vulkan layer audit count=166 unsupported=0') != 8 or audit.count('requested use_subgroup_ops=0 before load_param/model') != 8 or audit.count('fp16-packed=1 fp16-storage=1 fp16-arithmetic=0 input-pack=4 input-bits=16') != 8:
        raise ValueError('eight strict Vulkan/FP16 pose executions not proven')
    models=[]
    for role,folder in (('detector',detector_dir),('body',pose_dir)):
        hashes=MODEL_HASHES[role];target=output/role;target.mkdir(parents=True,exist_ok=True)
        for kind in ('param','bin'):
            source=folder/('vulkan.param' if role=='body' and kind=='param' else 'model.'+kind)
            require_hash(source,hashes[kind],role+' '+kind);shutil.copyfile(source,target/('model.'+kind))
        shapes={'cls':[1,2100,1],'bbox':[1,2100,4]} if role=='detector' else {'simcc_x':[1,26,384],'simcc_y':[1,26,512]}
        contract={'decoder':DECODERS[role],'output_blobs':list(shapes),'max_output_bytes':{}}
        for name,shape in shapes.items():
            size=4
            for dim in shape:size*=dim
            contract[name]={'shape':shape,'download_dtype':'fp32','max_bytes':size};contract['max_output_bytes'][name]=size
        models.append({'role':role,'format':'ncnn','decoder_id':DECODERS[role],
            'param_path':f'{role}/model.param','param_sha256':hashes['param'],
            'bin_path':f'{role}/model.bin','bin_sha256':hashes['bin'],'checkpoint_sha256':hashes['checkpoint'],
            'input_contract':model_input_contract(role),'output_contract':contract,'backend_options':copy.deepcopy(OPTIONS[role]),
            'source':pinned_source_url(role),'source_revisions':PINNED_REVISIONS,'license':LICENSE,
            'onnx_sha256':detector['artifacts']['onnx_sha256'] if role=='detector' else POSE_ONNX_SHA256,
            'onnx2ncnn_sha256':detector['onnx2ncnn_sha256'] if role=='detector' else CONVERTER_SHA256,
            'ncnnoptimize_sha256':OPTIMIZER_SHA256,
            'conversion_recipe':detector['conversion_command'] if role=='detector' else 'python -m tools.models.ncnn.prepare_rtmpose_eval --help; pinned official export, first-Conv zero padding, first ScaleNorm ReduceL2, MMDeploy converter, ncnnoptimize 65536, 12 shape-only Reshapes'})
    evidence=[]
    for source,name in ((pose_dir/'golden/index.json','pose-golden-index.json'),(pose_dir/'golden/vkmat-route.json','pose-vkmat-route.json'),(pose_dir/'detector/index.json','detector-regression-index.json'),
                        (ROOT/'third_party/ncnn/provenance.json','ncnn-provenance.json'),(ROOT/'out/c2-vendor/mmpose/LICENSE','OpenMMLab-source-LICENSE'),
                        (pose_dir/'conversion.json','pose-conversion.json'),(detector_dir/'model.json','detector-conversion.json')):
        target=output/'provenance'/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        evidence.append({'path':'provenance/'+name,'sha256':sha256_file(target)})
    for source in sorted((pose_dir/'golden/route-logs').glob('*.log')):
        target=output/'provenance/route-logs'/source.name
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        evidence.append({'path':'provenance/route-logs/'+source.name,'sha256':sha256_file(target)})
    manifest={'schema_version':2,'pack_id':PACK_ID,'pack_version':'0.4.0-preview.1','profile_id':PROFILE_ID,
        'profile_sha256':sha256_file(ROOT/'profiles'/f'{PROFILE_ID}.json'),'pipeline_id':'pipeline.topdown','max_people':8,
        'capabilities':CAPABILITIES,'local_evaluation_only':True,'models':models,'evidence':evidence}
    verify_pack(output,manifest)
    (output/'modelpack.json').write_text(canonical_json(manifest),encoding='utf-8',newline='\n')
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--detector-dir',type=Path,default=ROOT/'out/c2-local-detector')
    parser.add_argument('--pose-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    print(canonical_json(build_pack(args.detector_dir,args.pose_dir,args.output)))
