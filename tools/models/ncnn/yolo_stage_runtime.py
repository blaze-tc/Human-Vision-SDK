"""Create a separate ignored, local evaluation-only FP32 runtime root."""
import argparse
import json
import shutil
from pathlib import Path
from yolo_pose_gate import MODEL_HASHES, REVISION, sha, compare_directory, execution_options, LIMITS, runner_recipe

ROOT=Path(__file__).resolve().parents[3]
SGEMM_EVIDENCE_SHA='1cb1dec026bdc920371a73bfd6da1abbcb88fa6ae22130e0894838b6e69ab104'
SGEMM_CONTRACT='raw_tensor_fp32_sgemm_v1'

def validate_sgemm_evidence():
    """Recompute the entire frozen numerical gate before writing runtime files."""
    path=Path(__file__).with_name('yolo_sgemm_gate_evidence.json')
    if sha(path)!=SGEMM_EVIDENCE_SHA: raise ValueError('Reviewed SGEMM evidence index hash mismatch')
    evidence=json.loads(path.read_text())
    mode='gpu-fp32-sgemm'
    if (evidence['status']!='ELIGIBLE_FP32_SGEMM_OFFLINE_ONLY' or evidence['precision_mode']!=mode
        or evidence['hardware_fps_acceptance'] is not False or evidence['integration_completed'] is not False
        or evidence['options']!=execution_options(mode) or evidence['limits']!=LIMITS):
        raise ValueError('SGEMM offline options/limits identity mismatch')
    source,recipe=runner_recipe(mode)
    if sha(source)!=evidence['runner_source_sha256'] or sha(recipe)!=evidence['runner_cmake_sha256']:
        raise ValueError('SGEMM source/recipe hash mismatch')
    expected={'seven-416','one-416','seven-320','one-320','seven-640','one-640','empty-416',
              'seven-square320','one-square320','seven-square416','one-square416'}
    if len(evidence['fixtures'])!=11 or {f['fixture'] for f in evidence['fixtures']}!=expected:
        raise ValueError('SGEMM requires all eleven reviewed fixtures')
    for fixture in evidence['fixtures']:
        run=ROOT/'out'/fixture['archive']
        for name,key in [('fixture.json','fixture_sha256'),('comparison.json','comparison_sha256'),
                         ('execution.json','execution_sha256'),('input.fp32','input_sha256')]:
            if sha(run/name)!=fixture[key]: raise ValueError('SGEMM archive metadata hash mismatch')
        if sha(run/'runner')!=evidence['runner_sha256']: raise ValueError('SGEMM runner hash mismatch')
        for name,digest in fixture['output_sha256'].items():
            if sha(run/name)!=digest: raise ValueError('SGEMM output hash mismatch')
        for name,digest in fixture['cpu_output_sha256'].items():
            if sha(run/name)!=digest or sha(ROOT/'out'/fixture['historical_cpu_archive']/name)!=digest:
                raise ValueError('SGEMM historical CPU oracle hash mismatch')
        report=compare_directory(run,mode)
        if (not report['passed'] or not fixture['passed'] or report['limits']!=LIMITS
            or len(report['reference'])!=fixture['reference_people'] or len(report['actual'])!=fixture['gpu_people']):
            raise ValueError('Fresh SGEMM numerical gate failed: '+fixture['fixture'])
        if fixture['fixture']=='seven-640':
            metadata=json.loads((run/'fixture.json').read_text())
            if metadata['sequential_frame_index']!=1500 or len(report['actual'])!=7:
                raise ValueError('SGEMM raised-arm source frame identity mismatch')
            for person in report['actual']:
                shoulder,wrist=person['joints'][5],person['joints'][9]
                if not (shoulder[2]>=.2 and wrist[2]>=.2 and wrist[1]<shoulder[1]
                        and all(0<=joint[0]<1024 and 0<=joint[1]<576 for joint in (shoulder,wrist))):
                    raise ValueError('Fresh SGEMM seven raised left-arm semantic gate failed')
    return path,evidence

def shape_contract(size):
    if size not in (320,416,640): raise ValueError('Only reviewed square320/416 and rectangle640x384 are supported')
    return (f'rectangle640x384',640,384,'seven-640') if size==640 else (f'square{size}',size,size,f'seven-square{size}')

def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')

def stage(size,destination,convolution_kernel='default'):
    if convolution_kernel not in ('default','sgemm'): raise ValueError('Unsupported convolution kernel')
    sgemm=convolution_kernel=='sgemm'
    if sgemm and size!=640: raise ValueError('SGEMM runtime is reviewed only for rectangle640x384')
    shape,width,height,fixture_name=shape_contract(size)
    destination=destination.resolve()
    if not destination.is_relative_to((ROOT/'out/android-yolo').resolve()):
        raise ValueError('Evaluation runtime root must stay inside ignored out/android-yolo')
    if destination.exists(): raise ValueError('Choose a fresh runtime root; existing artifacts are preserved')
    if sgemm:evidence_path,evidence=validate_sgemm_evidence()
    else:
        evidence_path=Path(__file__).with_name('yolo_model_gate_evidence.json')
        evidence=json.loads(evidence_path.read_text())
    if not sgemm and (evidence['status']!='ELIGIBLE_FP32_OFFLINE_ONLY' or evidence['precision_mode']!='gpu-fp32'):
        raise ValueError('FP32 eligibility evidence missing')
    fixture=next(x for x in evidence['fixtures'] if x['fixture']==fixture_name)
    if not fixture['passed'] or fixture['reference_people']!=7 or fixture['gpu_people']!=7:
        raise ValueError('Reviewed seven-person eligibility failed')
    expected_archive='android-yolo/device-runs/seven-640/'+('gpu-fp32-sgemm-bb9d3dd06afd4311b27bae810c35a9fa' if sgemm else 'gpu-fp32-866e333068bf4b058ca0c40176faf897')
    if size==640 and fixture['archive']!=expected_archive:
        raise ValueError('Rectangle640x384 must bind the frozen M1 FP32 archive')
    run=ROOT/'out'/fixture['archive']
    if sha(run/'fixture.json')!=fixture['fixture_sha256'] or sha(run/'comparison.json')!=fixture['comparison_sha256'] or sha(run/'execution.json')!=fixture['execution_sha256']:
        raise ValueError('Eligibility archive metadata hash mismatch')
    metadata=json.loads((run/'fixture.json').read_text())
    if metadata['geometry']['width']!=width or metadata['geometry']['height']!=height or sha(run/'input.fp32')!=fixture['input_sha256']:
        raise ValueError('Eligibility input hash/shape mismatch')
    if size==640 and metadata['geometry']!={'width':640,'height':384,'resized_width':640,'resized_height':360,
        'left':0,'top':12,'scale':.625,'source_width':1024,'source_height':576}:
        raise ValueError('Frozen rectangle source geometry mismatch')
    for name,digest in fixture['output_sha256'].items():
        if sha(run/name)!=digest: raise ValueError('Eligibility output hash mismatch')
    assets=ROOT/'out/android-yolo/upstream/app/src/main/assets'
    for name,digest in MODEL_HASHES.items():
        if sha(assets/name)!=digest: raise ValueError('Pinned upstream weight hash mismatch')
    pack_id=f'yolov8n-pose-{shape}-fp32'+('-sgemm' if sgemm else '')+'-local'
    execution_contract=SGEMM_CONTRACT if sgemm else 'raw_tensor_fp32_v1'
    capabilities=['body_pose','multi_person','gpu_input','vulkan','android-hardware-buffer','external-sync-fd']
    profile={'schema_version':1,'profile':'android-ncnn-vulkan','local_evaluation_only':True,
        'frame_policy':'every_frame','body':{'pipeline':'pipeline.yolo.pose','modelPack':pack_id},
        'hands':{'enabled':False},'backend':{'preference':['backend.ncnn.vulkan'],'allow_fallback':False},
        'body_fps':30,'output':{'hz':60},'required_capabilities':capabilities}
    profile_path=destination/'profiles/android-ncnn-vulkan.json';write(profile_path,profile)
    pack_root=destination/'modelpacks'/pack_id;pack_root.mkdir(parents=True)
    for name in MODEL_HASHES: shutil.copyfile(assets/name,pack_root/name)
    anchors=sum((width//stride)*(height//stride) for stride in (8,16,32))
    model={'role':'body','format':'ncnn','decoder_id':'yolov8_pose_dfl17_v1','execution_contract':execution_contract,
        'param_path':'yolov8n_pose.ncnn.param','param_sha256':MODEL_HASHES['yolov8n_pose.ncnn.param'],
        'bin_path':'yolov8n_pose.ncnn.bin','bin_sha256':MODEL_HASHES['yolov8n_pose.ncnn.bin'],
        'source':f'https://github.com/nihui/ncnn-android-yolov8/tree/{REVISION}',
        'license':'Ultralytics origin; distribution rights unestablished; local evaluation only',
        'conversion_recipe':'Pinned upstream ncnn graph; M1 FP32 square input eligibility' if size!=640 else 'Pinned upstream ncnn graph; M1 FP32 rectangle640x384 input eligibility',
        'backend_options':{'use_packing_layout':True,'use_subgroup_ops':False,'use_fp16_packed':False,
                           'use_fp16_storage':False,'use_fp16_arithmetic':False},
        'input_contract':{'image_format':'rgba8-unorm','color_order':'rgb','crop':'letterbox',
            'resize_interpolation':'bilinear','pad_rgb':[114,114,114],'normalization':{'mean':[0,0,0],'norm':[1/255]*3},
            'width':width,'height':height,'tensor_dtype':'fp32','elempack':1,'input_blob':'in0'},
        'output_contract':{'decoder':'yolov8_pose_dfl17_v1','output_blobs':['out0','out1'],
            'max_output_bytes':{'out0':anchors*65*4,'out1':anchors*51*4}}}
    if sgemm:
        model['backend_options'].update(use_winograd_convolution=False,use_sgemm_convolution=True)
        model['conversion_recipe']='Pinned upstream ncnn graph; reviewed FP32 SGEMM eleven-fixture offline and rectangle640x384 frame1500 raised-left-arm eligibility'
    manifest={'schema_version':2,'pack_id':pack_id,'pack_version':'0.4.0-local','pipeline_id':'pipeline.yolo.pose',
        'local_evaluation_only':True,'execution_contract':execution_contract,'max_people':8,
        'capabilities':capabilities,'profile_sha256':sha(profile_path),'models':[model]}
    write(pack_root/'modelpack.json',manifest)
    index={'local_evaluation_only':True,'precision':'fp32','size':size,
        'hardware_fps_acceptance':False,'eligibility_evidence_sha256':sha(evidence_path),
        'fixture':fixture['archive'],'files':{str(p.relative_to(destination)).replace('\\','/'):sha(p)
        for p in sorted(destination.rglob('*')) if p.is_file()}}
    if size==640:index.update(shape_id=shape,source_aspect_ratio='16:9',input_width=width,input_height=height)
    if sgemm:index.update(convolution_kernel='sgemm',execution_contract=execution_contract,
        eligibility_archives=[f['archive'] for f in evidence['fixtures']],
        runner_sha256=evidence['runner_sha256'],runner_source_sha256=evidence['runner_source_sha256'],
        runner_cmake_sha256=evidence['runner_cmake_sha256'],raised_arm_semantic=evidence['raised_arm_semantic'])
    write(destination/'index.json',index)
    return destination

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--size',type=int,choices=(320,416,640),default=320)
    p.add_argument('--destination',type=Path);p.add_argument('--convolution-kernel',choices=('default','sgemm'),default='default');a=p.parse_args()
    suffix='-sgemm-verified' if a.convolution_kernel=='sgemm' else ''
    print(stage(a.size,a.destination or ROOT/f'out/android-yolo/runtime-{shape_contract(a.size)[0]}{suffix}',a.convolution_kernel))
