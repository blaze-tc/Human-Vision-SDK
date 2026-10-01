"""Create a separate ignored, local evaluation-only FP32 runtime root."""
import argparse
import json
import shutil
from pathlib import Path
from yolo_pose_gate import MODEL_HASHES, REVISION, sha, compare_directory, execution_options, LIMITS, runner_recipe

ROOT=Path(__file__).resolve().parents[3]
SGEMM_EVIDENCE_SHA='1cb1dec026bdc920371a73bfd6da1abbcb88fa6ae22130e0894838b6e69ab104'
SGEMM_CONTRACT='raw_tensor_fp32_sgemm_v1'
NO_LOCAL_MEMORY_EVIDENCE_SHA='211a59e74cfe3d8d0d96b836f0bbe626d844e863e283acc955b2c3dfd7af5680'
NO_LOCAL_MEMORY_CONTRACT='raw_tensor_fp32_no_local_memory_v1'
RECTANGLE512_EVIDENCE_SHA='fe23f93f39a77ad8fc3f3b78a60dbb728e34749116fcd22478074b5d493dde74'

RECTANGLE576_EVIDENCE_SHA='c3389a91ff01e4bfe2a293b52b67b0fc21608ba9cb5916ab60786259acd52cee'

def validate_rectangle576_evidence():
    """Recompute all three real CPU/GPU archives before creating a runtime."""
    from yolo_rectangle576_prepare import validate_fixture, SEVEN_VIDEO
    path=Path(__file__).with_name('yolo_rectangle576_gate_evidence.json')
    if sha(path)!=RECTANGLE576_EVIDENCE_SHA: raise ValueError('Reviewed rectangle576 evidence index hash mismatch')
    evidence=json.loads(path.read_text())
    if (evidence['status']!='ELIGIBLE_FP32_RECTANGLE576_OFFLINE_SINGLE_FRAME_ONLY_PENDING_INDEPENDENT_REVIEW'
        or evidence['precision_mode']!='gpu-fp32' or evidence['target_long_side']!=576
        or any(evidence[key] is not False for key in ('hardware_fps_acceptance','production_gpu_ahb_input_acceptance','integration_completed'))
        or evidence['options']!=execution_options('gpu-fp32') or evidence['limits']!=LIMITS
        or evidence['model_hashes']!=MODEL_HASHES or evidence['upstream_revision']!=REVISION
        or evidence['archive_base']!='out/android-yolo/rectangle576-eligibility-20261001'
        or len(evidence['fixtures'])!=3 or {f['fixture'] for f in evidence['fixtures']}!={'seven-576','one-576','empty-576'}):
        raise ValueError('Rectangle576 frozen options/limits/provenance mismatch')
    source,recipe=runner_recipe('gpu-fp32')
    if sha(source)!=evidence['runner_source_sha256'] or sha(recipe)!=evidence['runner_cmake_sha256']:
        raise ValueError('Rectangle576 runner source/recipe hash mismatch')
    base=(ROOT/evidence['archive_base']).resolve()
    for fixture in evidence['fixtures']:
        run=(base/fixture['archive']).resolve()
        if not run.is_relative_to(base): raise ValueError('Rectangle576 archive escaped frozen base')
        for name,key in [('fixture.json','fixture_sha256'),('comparison.json','comparison_sha256'),
                         ('execution.json','execution_sha256'),('input.fp32','input_sha256'),('source.png','source_png_sha256')]:
            if sha(run/name)!=fixture[key]: raise ValueError('Rectangle576 archive metadata/source hash mismatch')
        if sha(run/'runner')!=evidence['runner_sha256'] or fixture['runner_sha256']!=evidence['runner_sha256']:
            raise ValueError('Rectangle576 runner hash mismatch')
        for hashes in ('output_sha256','log_sha256'):
            for name,digest in fixture[hashes].items():
                artifact=(run/name).resolve()
                if not artifact.is_relative_to(run) or sha(artifact)!=digest: raise ValueError('Rectangle576 output/log hash mismatch')
        metadata=validate_fixture(run)
        if metadata['geometry']!=fixture['geometry'] or metadata['raw_shapes']!=fixture['raw_shapes']:
            raise ValueError('Rectangle576 source geometry/raw shapes mismatch')
        if fixture['fixture']=='seven-576' and sha(Path(metadata['source_video']))!=SEVEN_VIDEO:
            raise ValueError('Rectangle576 actual source video hash mismatch')
        report=compare_directory(run,'gpu-fp32')
        if (report!=json.loads((run/'comparison.json').read_text()) or not report['passed'] or not fixture['passed']
            or len(report['reference'])!=fixture['expected_people'] or len(report['actual'])!=fixture['expected_people']):
            raise ValueError('Fresh rectangle576 numerical gate failed: '+fixture['fixture'])
        if fixture['fixture']=='seven-576':
            semantic=fixture['arm_semantics'];saved=json.loads((run/semantic['report']).read_text())
            if sha(run/semantic['report'])!=semantic['report_sha256'] or not semantic['passed'] or not saved['passed']:
                raise ValueError('Rectangle576 arm semantic archive hash mismatch')
            for side in ('reference','actual'):
                matches=report['annotation_'+side]['matches']
                rows=[]
                for match in matches:
                    person=report[side][match['body']];shoulder,wrist=person['joints'][5],person['joints'][9]
                    passed=shoulder[2]>=.2 and wrist[2]>=.2 and wrist[1]<shoulder[1] and all(0<=p[0]<1024 and 0<=p[1]<576 for p in (shoulder,wrist))
                    rows.append({'person':match['person'],'body':match['body'],'annotation_iou':match['iou'],'left_shoulder':shoulder,'left_wrist':wrist,'passed':passed})
                if len(rows)!=7 or not all(row['passed'] for row in rows) or saved[side]!={'raised_arms':7,'people':rows}:
                    raise ValueError('Fresh rectangle576 seven raised left-arm semantic gate failed')
    return path,evidence

def validate_rectangle512_evidence():
    """Recheck both actual-device archives and CPU/GPU arms before any write."""
    path=Path(__file__).with_name('yolo_rectangle512_gate_evidence.json')
    if sha(path)!=RECTANGLE512_EVIDENCE_SHA: raise ValueError('Reviewed rectangle512 evidence index hash mismatch')
    evidence=json.loads(path.read_text())
    if (evidence['status']!='ELIGIBLE_FP32_RECTANGLE512_OFFLINE_SINGLE_FRAME_ONLY_PENDING_INDEPENDENT_REVIEW'
        or evidence['precision_mode']!='gpu-fp32' or evidence['target_long_side']!=512
        or any(evidence[key] is not False for key in ('hardware_fps_acceptance','production_gpu_ahb_input_acceptance','integration_completed'))
        or evidence['options']!=execution_options('gpu-fp32') or evidence['limits']!=LIMITS
        or evidence['model_hashes']!=MODEL_HASHES or evidence['upstream_revision']!=REVISION
        or evidence['archive_base']!='out/android-yolo/resolution-feasibility'
        or len(evidence['fixtures'])!=2 or {f['fixture'] for f in evidence['fixtures']}!={'seven-512','one-512'}):
        raise ValueError('Rectangle512 frozen options/limits/provenance mismatch')
    source,recipe=runner_recipe('gpu-fp32')
    if sha(source)!=evidence['runner_source_sha256'] or sha(recipe)!=evidence['runner_cmake_sha256']:
        raise ValueError('Rectangle512 runner source/recipe hash mismatch')
    for fixture in evidence['fixtures']:
        base=(ROOT/evidence['archive_base']).resolve();run=(base/fixture['archive']).resolve()
        if not run.is_relative_to(base): raise ValueError('Rectangle512 archive escaped frozen base')
        for name,key in [('fixture.json','fixture_sha256'),('comparison.json','comparison_sha256'),
                         ('execution.json','execution_sha256'),('input.fp32','input_sha256'),('source.png','source_png_sha256')]:
            if sha(run/name)!=fixture[key]: raise ValueError('Rectangle512 archive metadata/source hash mismatch')
        if sha(run/'runner')!=evidence['runner_sha256'] or fixture['runner_sha256']!=evidence['runner_sha256']:
            raise ValueError('Rectangle512 runner hash mismatch')
        for hashes in ('output_sha256','log_sha256'):
            for name,digest in fixture[hashes].items():
                if sha(run/name)!=digest: raise ValueError('Rectangle512 output/log hash mismatch')
        metadata=json.loads((run/'fixture.json').read_text())
        if metadata['geometry']!=fixture['geometry'] or metadata['limits']!=LIMITS or metadata['source_video_sha256']!=fixture['source_video_sha256']:
            raise ValueError('Rectangle512 source/geometry/limits mismatch')
        if sha(Path(metadata['source_video']))!=fixture['source_video_sha256']:
            raise ValueError('Rectangle512 actual source video hash mismatch')
        report=compare_directory(run,'gpu-fp32')
        if (not report['passed'] or not fixture['passed'] or report['limits']!=LIMITS
            or len(report['reference'])!=fixture['expected_people'] or len(report['actual'])!=fixture['expected_people']):
            raise ValueError('Fresh rectangle512 numerical gate failed: '+fixture['fixture'])
        if fixture['fixture']=='seven-512':
            if (metadata['sequential_frame_index']!=1500 or metadata['source_rgba_sha256']!='83db08727c2db6aafe2369e71f689428c3e9197176f2d10933a839b141ab9b67'
                or metadata['geometry']!={'width':512,'height':288,'resized_width':512,'resized_height':288,'left':0,'top':0,'scale':.5,'source_width':1024,'source_height':576}):
                raise ValueError('Rectangle512 raised-arm source identity mismatch')
            semantic=fixture['arm_semantics']
            if sha(run/semantic['report'])!=semantic['report_sha256'] or not semantic['passed']:
                raise ValueError('Rectangle512 arm semantic archive hash mismatch')
            for people in (report['reference'],report['actual']):
                if len(people)!=7: raise ValueError('Rectangle512 requires seven raised arms')
                for person in people:
                    shoulder,wrist=person['joints'][5],person['joints'][9]
                    if not (shoulder[2]>=.2 and wrist[2]>=.2 and wrist[1]<shoulder[1]
                            and all(0<=joint[0]<1024 and 0<=joint[1]<576 for joint in (shoulder,wrist))):
                        raise ValueError('Fresh rectangle512 seven raised left-arm semantic gate failed')
    return path,evidence

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

def validate_no_local_memory_evidence():
    """Recompute the entire frozen numerical gate before writing runtime files."""
    path=Path(__file__).with_name('yolo_no_local_memory_gate_evidence.json')
    if sha(path)!=NO_LOCAL_MEMORY_EVIDENCE_SHA: raise ValueError('Reviewed NO_LOCAL_MEMORY evidence index hash mismatch')
    evidence=json.loads(path.read_text())
    mode='gpu-fp32-no-local-memory'
    if (evidence['status']!='ELIGIBLE_FP32_NO_LOCAL_MEMORY_OFFLINE_ONLY' or evidence['precision_mode']!=mode
        or evidence['hardware_fps_acceptance'] is not False or evidence['integration_completed'] is not False
        or evidence['performance_claim'] is not False or evidence['cpu_hash_matches']!=22
        or evidence['model_hashes']!=MODEL_HASHES or evidence['model_upstream_revision']!=REVISION
        or evidence['options']!=execution_options(mode) or evidence['cpu_options']!=execution_options('cpu') or evidence['limits']!=LIMITS):
        raise ValueError('No-local-memory offline options/limits identity mismatch')
    source,recipe=runner_recipe(mode)
    if sha(source)!=evidence['runner_source_sha256'] or sha(recipe)!=evidence['runner_cmake_sha256']:
        raise ValueError('NO_LOCAL_MEMORY source/recipe hash mismatch')
    expected={'seven-416','one-416','seven-320','one-320','seven-640','one-640','empty-416',
              'seven-square320','one-square320','seven-square416','one-square416'}
    if len(evidence['fixtures'])!=11 or {f['fixture'] for f in evidence['fixtures']}!=expected:
        raise ValueError('NO_LOCAL_MEMORY requires all eleven reviewed fixtures')
    for fixture in evidence['fixtures']:
        run=ROOT/'out'/fixture['archive']
        for name,key in [('fixture.json','fixture_sha256'),('comparison.json','comparison_sha256'),
                         ('execution.json','execution_sha256'),('input.fp32','input_sha256'),('source.png','source_png_sha256')]:
            if sha(run/name)!=fixture[key]: raise ValueError('NO_LOCAL_MEMORY archive metadata hash mismatch')
        if sha(run/'runner')!=evidence['runner_sha256']: raise ValueError('NO_LOCAL_MEMORY runner hash mismatch')
        for name,digest in fixture['output_sha256'].items():
            if sha(run/name)!=digest: raise ValueError('NO_LOCAL_MEMORY output hash mismatch')
        for name,digest in fixture['cpu_output_sha256'].items():
            if sha(run/name)!=digest or sha(ROOT/'out'/fixture['historical_cpu_archive']/name)!=digest:
                raise ValueError('NO_LOCAL_MEMORY historical CPU oracle hash mismatch')
        for stage in fixture['stages'].values():
            if sha(run/stage['log_file'])!=stage['log_sha256']: raise ValueError('No-local-memory log hash mismatch')
        metadata=json.loads((run/'fixture.json').read_text())
        if metadata['geometry']!=fixture['geometry'] or metadata.get('source_video_sha256')!=fixture['source_video_sha256'] or (fixture['source_video_sha256'] is not None and sha(Path(metadata['source_video']))!=fixture['source_video_sha256']):
            raise ValueError('No-local-memory source/geometry mismatch')
        report=compare_directory(run,mode)
        if report!=json.loads((run/'comparison.json').read_text()):
            raise ValueError('Fresh no-local-memory numerical gate failed: archived comparison differs')
        if (not report['passed'] or not fixture['passed'] or report['limits']!=LIMITS
            or len(report['reference'])!=fixture['reference_people'] or len(report['actual'])!=fixture['gpu_people']):
            raise ValueError('Fresh no-local-memory numerical gate failed: '+fixture['fixture'])
        if fixture['fixture']=='seven-640':
            metadata=json.loads((run/'fixture.json').read_text())
            if metadata['sequential_frame_index']!=1500 or len(report['actual'])!=7:
                raise ValueError('NO_LOCAL_MEMORY raised-arm source frame identity mismatch')
            for person in report['reference']+report['actual']:
                shoulder,wrist=person['joints'][5],person['joints'][9]
                if not (shoulder[2]>=.2 and wrist[2]>=.2 and wrist[1]<shoulder[1]
                        and all(0<=joint[0]<1024 and 0<=joint[1]<576 for joint in (shoulder,wrist))):
                    raise ValueError('Fresh no-local-memory seven raised left-arm semantic gate failed')
    return path,evidence

def shape_contract(size):
    if size==576:return ('rectangle576x352',576,352,'seven-576')
    if size==512:return ('rectangle512x288',512,288,'seven-512')
    if size not in (320,416,640): raise ValueError('Only reviewed square320/416 and rectangle640x384 are supported')
    return (f'rectangle640x384',640,384,'seven-640') if size==640 else (f'square{size}',size,size,f'seven-square{size}')

def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8')

def stage(size,destination,convolution_kernel='default'):
    if convolution_kernel not in ('default','sgemm','no-local-memory'): raise ValueError('Unsupported convolution kernel')
    sgemm=convolution_kernel=='sgemm'
    no_local_memory=convolution_kernel=='no-local-memory'
    if no_local_memory and size!=640: raise ValueError('No-local-memory runtime is reviewed only for rectangle640x384')
    if sgemm and size!=640: raise ValueError('SGEMM runtime is reviewed only for rectangle640x384')
    shape,width,height,fixture_name=shape_contract(size)
    destination=destination.resolve()
    if not destination.is_relative_to((ROOT/'out/android-yolo').resolve()):
        raise ValueError('Evaluation runtime root must stay inside ignored out/android-yolo')
    if destination.exists(): raise ValueError('Choose a fresh runtime root; existing artifacts are preserved')
    if size==576:evidence_path,evidence=validate_rectangle576_evidence()
    elif size==512:evidence_path,evidence=validate_rectangle512_evidence()
    elif no_local_memory:evidence_path,evidence=validate_no_local_memory_evidence()
    elif sgemm:evidence_path,evidence=validate_sgemm_evidence()
    else:
        evidence_path=Path(__file__).with_name('yolo_model_gate_evidence.json')
        evidence=json.loads(evidence_path.read_text())
    if not sgemm and not no_local_memory and size not in (512,576) and (evidence['status']!='ELIGIBLE_FP32_OFFLINE_ONLY' or evidence['precision_mode']!='gpu-fp32'):
        raise ValueError('FP32 eligibility evidence missing')
    fixture=next(x for x in evidence['fixtures'] if x['fixture']==fixture_name)
    if not fixture['passed'] or fixture['reference_people']!=7 or fixture['gpu_people']!=7:
        raise ValueError('Reviewed seven-person eligibility failed')
    expected_archive='android-yolo/device-runs/seven-640/'+('gpu-fp32-no-local-memory-5c7bb015e67f4cef9129dddec5b5dcf9' if no_local_memory else 'gpu-fp32-sgemm-bb9d3dd06afd4311b27bae810c35a9fa' if sgemm else 'gpu-fp32-866e333068bf4b058ca0c40176faf897')
    if size==640 and fixture['archive']!=expected_archive:
        raise ValueError('Rectangle640x384 must bind the frozen M1 FP32 archive')
    run=ROOT/evidence['archive_base']/fixture['archive'] if size in (512,576) else ROOT/'out'/fixture['archive']
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
    pack_id=f'yolov8n-pose-{shape}-fp32'+('-no-local-memory' if no_local_memory else '-sgemm' if sgemm else '')+'-local'
    execution_contract=NO_LOCAL_MEMORY_CONTRACT if no_local_memory else SGEMM_CONTRACT if sgemm else 'raw_tensor_fp32_v1'
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
    if no_local_memory:
        model['backend_options'].update(use_winograd_convolution=True,use_sgemm_convolution=True,use_shader_local_memory=False)
        model['conversion_recipe']='Pinned upstream ncnn graph; reviewed FP32 no-local-memory eleven-fixture offline and rectangle640x384 frame1500 raised-left-arm eligibility'
    if sgemm:
        model['backend_options'].update(use_winograd_convolution=False,use_sgemm_convolution=True)
        model['conversion_recipe']='Pinned upstream ncnn graph; reviewed FP32 SGEMM eleven-fixture offline and rectangle640x384 frame1500 raised-left-arm eligibility'
    if size==576:model['conversion_recipe']='Pinned upstream ncnn graph; reviewed FP32 rectangle576x352 frame1500 raised-left-arm eligibility'
    if size==512:model['conversion_recipe']='Pinned upstream ncnn graph; reviewed FP32 rectangle512x288 frame1500 raised-left-arm eligibility'
    manifest={'schema_version':2,'pack_id':pack_id,'pack_version':'0.4.0-local','pipeline_id':'pipeline.yolo.pose',
        'local_evaluation_only':True,'execution_contract':execution_contract,'max_people':8,
        'capabilities':capabilities,'profile_sha256':sha(profile_path),'models':[model]}
    write(pack_root/'modelpack.json',manifest)
    index={'local_evaluation_only':True,'precision':'fp32','size':size,
        'hardware_fps_acceptance':False,'eligibility_evidence_sha256':sha(evidence_path),
        'fixture':fixture['archive'],'files':{str(p.relative_to(destination)).replace('\\','/'):sha(p)
        for p in sorted(destination.rglob('*')) if p.is_file()}}
    if size==640:index.update(shape_id=shape,source_aspect_ratio='16:9',input_width=width,input_height=height)
    if size in (512,576):index.update(shape_id=shape,source_aspect_ratio='16:9',input_width=width,input_height=height,
        fixture=str(run.relative_to(ROOT/'out')).replace('\\','/'),execution_contract=execution_contract,
        eligibility_archives=[str((ROOT/evidence['archive_base']/f['archive']).relative_to(ROOT/'out')).replace('\\','/') for f in evidence['fixtures']],
        runner_sha256=evidence['runner_sha256'],runner_source_sha256=evidence['runner_source_sha256'],
        runner_cmake_sha256=evidence['runner_cmake_sha256'],raised_arm_semantic=fixture['arm_semantics'])
    if sgemm or no_local_memory:index.update(convolution_kernel=convolution_kernel,execution_contract=execution_contract,
        eligibility_archives=[f['archive'] for f in evidence['fixtures']],
        runner_sha256=evidence['runner_sha256'],runner_source_sha256=evidence['runner_source_sha256'],
        runner_cmake_sha256=evidence['runner_cmake_sha256'],raised_arm_semantic=evidence['raised_arm_semantic'])
    write(destination/'index.json',index)
    return destination

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--size',type=int,choices=(320,416,512,576,640),default=320)
    p.add_argument('--destination',type=Path);p.add_argument('--convolution-kernel',choices=('default','sgemm','no-local-memory'),default='default');a=p.parse_args()
    suffix='-no-local-memory-verified' if a.convolution_kernel=='no-local-memory' else '-sgemm-verified' if a.convolution_kernel=='sgemm' else ''
    print(stage(a.size,a.destination or ROOT/f'out/android-yolo/runtime-{shape_contract(a.size)[0]}{suffix}',a.convolution_kernel))
