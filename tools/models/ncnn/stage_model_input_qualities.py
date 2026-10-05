"""Fresh local-only combined quality runtime; preserves every indexed PC/640 byte.

This is model eligibility/staging, never hardware FPS, APK or distribution approval.
Historical evidence is read only. 512 keeps its original runner; 960 uses Q1.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from yolo_pose_gate import MODEL_HASHES, sha, execution_options, LIMITS, REVISION
from yolo_stage_runtime import validate_rectangle512_evidence
from yolo_rectangle960_device_gate import compare_directory as compare960, runner_recipe
from tools.test.stage_android_yolo_eval import verify_runtime, verify_rectangle512_identity, INDEX_SHA

BASE_INDEX_SHA = '89703ad345f2b7a954b6711f8e65ffbd02e5f80cbf1c653d6815a53395ee3cfc'
HIGH_EVIDENCE_SHA = 'b1d634d79c6793bb0970e49b7b3975e901b9560a3f38f9f581e57e554de82683'
BASE_PROFILE = 'android-ncnn-vulkan'
SHAPES = [('low',512,288),('medium',640,384),('high',960,576)]
OPTIONS = dict(use_packing_layout=True,use_subgroup_ops=False,use_fp16_packed=False,use_fp16_storage=False,use_fp16_arithmetic=False)
CAPABILITIES = {'body_pose','multi_person','gpu_input','vulkan','android-hardware-buffer','external-sync-fd'}
MODEL_SOURCE = 'https://github.com/nihui/ncnn-android-yolov8/tree/'+REVISION
MODEL_LICENSE = 'Ultralytics origin; distribution rights unestablished; local evaluation only'


def conversion_recipe(width,height):
    return 'Pinned upstream ncnn graph; M1 FP32 rectangle640x384 input eligibility' if width==640 else f'Pinned upstream ncnn graph; reviewed FP32 rectangle{width}x{height} offline numerical and raised-left-arm eligibility; local evaluation only'


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def read(path):
    def pairs(rows):
        result={}
        for key,value in rows:
            if key in result: raise ValueError('Duplicate JSON key: '+key)
            result[key]=value
        return result
    return json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Nonfinite JSON value')))


def bound(root, relative):
    if not isinstance(relative,str) or not relative or any(c in relative for c in ('..',':','\\')) or any(p in ('','.') for p in relative.split('/')):
        raise ValueError('Unsafe runtime path')
    path=root/relative
    if not path.resolve().is_relative_to(root.resolve()): raise ValueError('Path escapes runtime')
    for current in (path,*path.parents):
        if current == root.parent: break
        if current.is_symlink() or current.is_junction(): raise ValueError('Linked runtime path')
    return path


def indexed_files(root):
    index=read(root/'index.json')
    if not isinstance(index.get('version'),str) or not index['version'] or not isinstance(index.get('files'),list) or not index['files']:
        raise ValueError('Runtime index requires version and files')
    files={}
    for row in index['files']:
        if not isinstance(row,dict) or set(row)!={'path','sha256'}: raise ValueError('Bad index entry')
        path=bound(root,row['path']); digest=row['sha256']
        if row['path'].lower() in {p.lower() for p in files}: raise ValueError('Duplicate runtime index path')
        if not isinstance(digest,str) or not re.fullmatch('[0-9a-f]{64}',digest) or not path.is_file() or sha(path)!=digest:
            raise ValueError('Runtime index file/hash mismatch: '+row['path'])
        files[row['path']]=digest
    return index,files


def profile_id(quality): return BASE_PROFILE if quality=='medium' else BASE_PROFILE+'-quality-'+quality


def validate_runtime(root):
    root=Path(root); index,files=indexed_files(root)
    path='model-input-qualities.json'
    if path not in files: raise ValueError('Quality catalog is not indexed')
    catalog=read(root/path)
    if type(catalog) is not dict or set(catalog)!={'schema_version','families'} or type(catalog['schema_version']) is not int or catalog['schema_version']!=1:
        raise ValueError('Unsupported quality catalog schema')
    if not isinstance(catalog['families'],list) or len(catalog['families'])!=1: raise ValueError('Exactly one admitted quality family required')
    family=catalog['families'][0]
    if set(family)!={'runtime_mode','base_profile','default_quality','qualities'} or family['runtime_mode']!=BASE_PROFILE or family['base_profile']!=BASE_PROFILE or family['default_quality']!='medium':
        raise ValueError('Quality family contract mismatch')
    if not isinstance(family['qualities'],list) or len(family['qualities'])!=3: raise ValueError('Exactly three qualities required')
    entries={}
    for q in family['qualities']:
        if not isinstance(q,dict) or set(q)!={'id','profile','width','height'} or q['id'] in entries: raise ValueError('Invalid/duplicate quality entry')
        entries[q['id']]=q
    if set(entries)!={'low','medium','high'}: raise ValueError('Unexpected quality IDs')
    for quality,width,height in SHAPES:
        q=entries[quality]; pid=profile_id(quality)
        if q['profile']!=pid or type(q['width']) is not int or type(q['height']) is not int or (q['width'],q['height'])!=(width,height):
            raise ValueError('Quality profile/geometry mismatch')
        relative='profiles/'+pid+'.json'
        if relative not in files: raise ValueError('Quality profile not indexed')
        profile=read(root/relative)
        if type(profile.get('schema_version')) is not int or profile.get('schema_version')!=1 or profile.get('profile')!=pid or profile.get('local_evaluation_only') is not True or profile.get('frame_policy')!='every_frame' or profile.get('hands')!={'enabled':False} or profile['hands']['enabled'] is not False or profile.get('backend')!={'preference':['backend.ncnn.vulkan'],'allow_fallback':False} or profile['backend']['allow_fallback'] is not False:
            raise ValueError('Quality profile composition mismatch')
        required=profile.get('required_capabilities')
        if not isinstance(required,list) or any(not isinstance(c,str) for c in required) or len(required)!=len(set(required)) or set(required)!=CAPABILITIES: raise ValueError('Quality profile required capabilities mismatch')
        pack_id=profile['body']['modelPack']
        if not re.fullmatch('[a-z0-9][a-z0-9._-]{0,63}',pack_id) or profile['body']['pipeline']!='pipeline.yolo.pose': raise ValueError('Invalid quality pack/pipeline')
        prefix='modelpacks/'+pack_id+'/'
        if (root/(prefix+'manifest.json')).exists() or prefix+'modelpack.json' not in files: raise ValueError('Ambiguous/unindexed quality manifest')
        pack=read(root/(prefix+'modelpack.json'))
        if type(pack.get('schema_version')) is not int or pack.get('schema_version')!=2 or not isinstance(pack.get('pack_version'),str) or not pack['pack_version'].strip() or pack.get('pack_id')!=pack_id or pack.get('pipeline_id')!='pipeline.yolo.pose' or type(pack.get('max_people')) is not int or pack.get('max_people')!=8 or pack.get('local_evaluation_only') is not True or pack.get('execution_contract')!='raw_tensor_fp32_v1' or pack.get('profile_sha256')!=files[relative] or ('profile_id' in pack and pack['profile_id']!=pid):
            raise ValueError('Quality pack/profile binding mismatch')
        if len(pack.get('models',[]))!=1: raise ValueError('One quality body model required')
        caps=pack.get('capabilities',[])
        if len(caps)!=len(set(caps)) or set(caps)!=CAPABILITIES: raise ValueError('Quality pack capabilities mismatch')
        model=pack['models'][0]
        if model.get('source')!=MODEL_SOURCE or model.get('license')!=MODEL_LICENSE or model.get('conversion_recipe')!=conversion_recipe(width,height): raise ValueError('Quality model pinned provenance metadata mismatch')
        anchors=sum(width//s*(height//s) for s in (8,16,32))
        expected_input=dict(image_format='rgba8-unorm',color_order='rgb',crop='letterbox',resize_interpolation='bilinear',pad_rgb=[114,114,114],normalization={'mean':[0,0,0],'norm':[1/255]*3},width=width,height=height,tensor_dtype='fp32',elempack=1,input_blob='in0')
        expected_output=dict(decoder='yolov8_pose_dfl17_v1',output_blobs=['out0','out1'],max_output_bytes={'out0':anchors*65*4,'out1':anchors*51*4})
        if model.get('role')!='body' or model.get('format')!='ncnn' or model.get('decoder_id')!='yolov8_pose_dfl17_v1' or model.get('execution_contract')!='raw_tensor_fp32_v1' or model.get('backend_options')!=OPTIONS or any(type(v) is not bool for v in model['backend_options'].values()) or model.get('input_contract')!=expected_input or any(type(model['input_contract'][key]) is not int for key in ('width','height','elempack')) or model.get('output_contract')!=expected_output or any(type(v) is not int for v in model['output_contract']['max_output_bytes'].values()):
            raise ValueError('Quality FP32 input/output/backend contract mismatch')
        for kind in ('param','bin'):
            name='yolov8n_pose.ncnn.'+kind
            if model.get(kind+'_path')!=name or model.get(kind+'_sha256')!=MODEL_HASHES[name] or files.get(prefix+name)!=MODEL_HASHES[name]: raise ValueError('Quality pinned model/index mismatch')
    return catalog


def validate_high_evidence():
    path=Path(__file__).with_name('yolo_rectangle960_gate_evidence.json')
    if sha(path)!=HIGH_EVIDENCE_SHA: raise ValueError('Q1 960 evidence hash differs')
    evidence=read(path)
    if evidence['options']!=execution_options('gpu-fp32') or evidence['limits']!=LIMITS or evidence['model_hashes']!=MODEL_HASHES or evidence['upstream_revision']!=REVISION or any(evidence[k] is not False for k in ('hardware_fps_acceptance','production_gpu_ahb_input_acceptance','integration_completed')) or {f['fixture'] for f in evidence['fixtures']}!={'seven-960','one-960','empty-960'} or len(evidence['fixtures'])!=3: raise ValueError('Q1 960 evidence contract differs')
    source,recipe=runner_recipe('gpu-fp32')
    if sha(source)!=evidence['runner_source_sha256'] or sha(recipe)!=evidence['runner_cmake_sha256']: raise ValueError('Q1 960 runner recipe differs')
    base=bound(ROOT,evidence['archive_base'])
    for fixture in evidence['fixtures']:
        run=bound(base,fixture['archive'])
        for name,digest in fixture['artifact_sha256'].items():
            if sha(bound(run,name))!=digest: raise ValueError('Q1 960 artifact hash differs')
        report=compare960(run)
        if not report['passed'] or not fixture['passed'] or report!=read(run/'comparison.json') or len(report['reference'])!=fixture['reference_people'] or len(report['actual'])!=fixture['gpu_people']: raise ValueError('Q1 960 numerical/semantic gate failed')
        if sha(run/'runner')!=evidence['runner_sha256']: raise ValueError('Q1 960 runner hash differs')
    return evidence


def stage(base, low, destination):
    base=Path(base).resolve(); low=Path(low).resolve(); destination=Path(destination).resolve()
    if destination.exists() or not destination.is_relative_to((ROOT/'out/android-yolo').resolve()): raise ValueError('Choose a fresh local output under out/android-yolo')
    if sha(base/'index.json')!=BASE_INDEX_SHA: raise ValueError('Reviewed combined PC/640 index hash differs')
    index,files=indexed_files(base)
    if sha(low/'index.json')!=INDEX_SHA[512]: raise ValueError('Frozen 512 runtime index hash differs')
    low_index,low_files=verify_runtime(low,512); verify_rectangle512_identity(low_index,low_files)
    validate_rectangle512_evidence()  # Original frozen 512 runner, never Q1's new 960 runner.
    high_evidence=validate_high_evidence()
    profile=read(base/'profiles/android-ncnn-vulkan.json'); original_pack=base/'modelpacks'/profile['body']['modelPack']
    if files['profiles/android-ncnn-vulkan.json']!='5cd72fca9ea22a276bf94bb279777ea687488eee89a5308f432ee48edc68185e': raise ValueError('Reviewed medium profile differs')
    # Copy only reviewed indexed bytes; omit unindexed extra files/caches.
    for relative in files:
        target=destination/relative; target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(base/relative,target)
    for quality,width,height in SHAPES:
        if quality=='medium': continue
        pid=profile_id(quality); pack_id=f'yolov8n-pose-rectangle{width}x{height}-fp32-quality-{quality}-local'
        source_pack=low/'modelpacks/yolov8n-pose-rectangle512x288-fp32-local' if quality=='low' else original_pack
        p=copy.deepcopy(profile); p['profile']=pid; p['body']['modelPack']=pack_id
        profile_path=destination/'profiles'/f'{pid}.json'; write(profile_path,p)
        pack=read(source_pack/'modelpack.json'); pack['pack_id']=pack_id; pack['profile_sha256']=sha(profile_path)
        if 'profile_id' in pack: pack['profile_id']=pid
        model=pack['models'][0]; model['input_contract'].update(width=width,height=height)
        n=sum(width//s*(height//s) for s in (8,16,32)); model['output_contract']['max_output_bytes']={'out0':n*65*4,'out1':n*51*4}
        model['conversion_recipe']=conversion_recipe(width,height)
        pack_root=destination/'modelpacks'/pack_id
        write(pack_root/'modelpack.json',pack)
        for name in MODEL_HASHES: shutil.copyfile(source_pack/name,pack_root/name)
    catalog={'schema_version':1,'families':[{'runtime_mode':BASE_PROFILE,'base_profile':BASE_PROFILE,'default_quality':'medium','qualities':[{'id':q,'profile':profile_id(q),'width':w,'height':h} for q,w,h in SHAPES]}]}
    write(destination/'model-input-qualities.json',catalog)
    index['version']='model-input-qualities-v1-local-fp32'
    index['quality_staging']={'base_index_sha256':BASE_INDEX_SHA,'low_evidence_sha256':sha(Path(__file__).with_name('yolo_rectangle512_gate_evidence.json')),'high_evidence_sha256':HIGH_EVIDENCE_SHA,'hardware_fps_acceptance':False,'production_gpu_ahb_input_acceptance':False,'distribution_qualified':False,'native_quality_deployment_qualified':False}
    index['files']=[{'path':p.relative_to(destination).as_posix(),'sha256':sha(p)} for p in sorted(destination.rglob('*')) if p.is_file()]
    write(destination/'index.json',index); validate_runtime(destination)
    return destination


if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--base',type=Path,default=ROOT/'out/input/production-correction/user-packages-v15/com.blazetc.humanvision/RuntimeData'); parser.add_argument('--low',type=Path,default=ROOT/'out/android-yolo/runtime-rectangle512x288-arm-verified'); parser.add_argument('--destination',type=Path,required=True)
    args=parser.parse_args(); print(stage(args.base,args.low,args.destination))
