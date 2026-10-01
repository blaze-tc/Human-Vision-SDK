"""Local-only Unity evaluation staging; never rebuild native or launch Unity."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE_SHA = 'f82c67c8e771ab264dcf891ce44b8b540028dbb04ea524797b2ce5c79bb09989'
SGEMM_NATIVE_SHA = '12cf4e1a5146b7368e86d513609fa9a3724211427b4eaa5c91351051f0e04928'
SGEMM_INDEX_SHA = 'dd397a0e2cf8e25e28dd65b46cb7b14ed779eb181ad82de10bfc81065a5bc346'
SGEMM_EVIDENCE_SHA = '1cb1dec026bdc920371a73bfd6da1abbcb88fa6ae22130e0894838b6e69ab104'
SGEMM_FIXTURE = 'android-yolo/device-runs/seven-640/gpu-fp32-sgemm-bb9d3dd06afd4311b27bae810c35a9fa'
SGEMM_SELECTED_FILES = {
    'profiles/android-ncnn-vulkan.json':'ccf964b0909255c68c3fc8f192fc3e58be5d4afbdec017ffb167bbeb595885cc',
    'modelpacks/yolov8n-pose-rectangle640x384-fp32-sgemm-local/modelpack.json':'7c56397ede6ff4b2461d33590002f34035b8ccecb1ff2ee0db29933770d503f3',
    'modelpacks/yolov8n-pose-rectangle640x384-fp32-sgemm-local/yolov8n_pose.ncnn.param':'908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905',
    'modelpacks/yolov8n-pose-rectangle640x384-fp32-sgemm-local/yolov8n_pose.ncnn.bin':'6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9'}
SGEMM_PROVENANCE = {
    'runner_sha256':'d754708bcb687bcb5413de06ce075fba1781cbe5972d9a9c2cc425451ed6c73f',
    'runner_source_sha256':'f5e2fbc7e545d0fea842038c797a7ca7cfe79e747067c91dcf93bb264d5f76bd',
    'runner_cmake_sha256':'819a695b322fa62c50d7c06e0405c781ade94cff16e578d52720dd870b8fe4e1'}
VIDEO_SHA = 'e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8'
INDEX_SHA = {320: '9119eb49528758d2b4d9237406fd98643e2d0c5d624804ee16769bff9d81ba69',
             416: '10454b1a73e4c86e1d6396e3de251ac398176ed24d31a0d09f7dd8969d7f6629',
             640: '954b0fb5a152672b2242ccc14d49588be74e488000a110bbe7c91a022b143f00'}

def shape_id(size):
    if size not in INDEX_SHA: raise ValueError('Unreviewed YOLO input shape')
    return 'rectangle640x384' if size==640 else f'square{size}'


def runtime_version(size, kernel):
    if kernel not in ('default', 'sgemm') or kernel=='sgemm' and size!=640:
        raise ValueError('Unreviewed convolution kernel or shape')
    return f'yolo-local-fp32-{shape_id(size)}' + ('-sgemm' if kernel=='sgemm' else '')


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require_new_output(path):
    if path.exists():
        raise FileExistsError('Output must be a new directory: ' + str(path))


def bound(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError('Path escapes runtime root: ' + relative)
    return path


def unity_index(index, files, size, kernel='default'):
    generated=dict(index)
    generated['version']=runtime_version(size,kernel)
    generated['files']=[{'path': p, 'sha256': h} for p,h in sorted(files.items())]
    return generated


def verify_sgemm_identity(index, files):
    if files != SGEMM_SELECTED_FILES or any(index.get(key)!=value for key,value in SGEMM_PROVENANCE.items()):
        raise ValueError('Reviewed SGEMM model/profile/source identity differs')


def verify_runtime(root, size, kernel='default'):
    version=runtime_version(size,kernel)
    index = json.loads((root/'index.json').read_text(encoding='utf-8-sig'))
    if index.get('size') != size or index.get('precision') != 'fp32' or index.get('local_evaluation_only') is not True:
        raise ValueError('Runtime eligibility metadata mismatch')
    if index.get('convolution_kernel','default') != kernel:
        raise ValueError('Explicit convolution kernel selection mismatch')
    if kernel=='sgemm' and index.get('eligibility_evidence_sha256')!=SGEMM_EVIDENCE_SHA:
        raise ValueError('Reviewed SGEMM eligibility identity differs')
    if size==640 and (index.get('shape_id')!='rectangle640x384' or index.get('input_width')!=640 or
        index.get('input_height')!=384 or index.get('source_aspect_ratio')!='16:9' or
        index.get('fixture')!=(SGEMM_FIXTURE if kernel=='sgemm' else 'android-yolo/device-runs/seven-640/gpu-fp32-866e333068bf4b058ca0c40176faf897')):
        raise ValueError('Frozen rectangle640x384 source/eligibility identity differs')
    files = index['files']
    if isinstance(files, list):
        if index.get('version') != version: raise ValueError('Unity index version missing or invalid')
        paths=[row['path'] for row in files]
        if len(set(paths)) != len(paths): raise ValueError('Duplicate Unity index entry')
        for row in files:
            if not row['path'] or row['path'].startswith('/') or any(token in row['path'] for token in ('..',':','\\')) or \
                len(row['sha256']) != 64 or any(c not in '0123456789abcdef' for c in row['sha256']):
                raise ValueError('Unsafe Unity index entry')
        files = {row['path']: row['sha256'] for row in files}
    for relative, digest in files.items():
        if sha256(bound(root, relative)) != digest: raise ValueError('Runtime file hash mismatch: ' + relative)
    profile_path = 'profiles/android-ncnn-vulkan.json'
    profile = json.loads(bound(root, profile_path).read_text(encoding='utf-8-sig'))
    if profile.get('local_evaluation_only') is not True or profile.get('frame_policy') != 'every_frame' or \
        profile['body']['pipeline'] != 'pipeline.yolo.pose' or profile['backend'] != {'preference': ['backend.ncnn.vulkan'], 'allow_fallback': False} or profile['hands']['enabled']:
        raise ValueError('Explicit YOLO profile selection mismatch')
    pack_root = bound(root, 'modelpacks/' + profile['body']['modelPack'])
    pack_path = (pack_root/'modelpack.json').relative_to(root.resolve()).as_posix()
    if profile_path not in files or pack_path not in files: raise ValueError('Selected profile/pack missing from index')
    pack = json.loads((pack_root/'modelpack.json').read_text(encoding='utf-8-sig'))
    if kernel=='sgemm' and (pack.get('execution_contract')!='raw_tensor_fp32_sgemm_v1' or
                           index.get('execution_contract')!='raw_tensor_fp32_sgemm_v1'):
        raise ValueError('SGEMM index/pack/model execution contract mismatch')
    if pack['pack_id'] != profile['body']['modelPack'] or pack['pipeline_id'] != 'pipeline.yolo.pose' or \
        pack['local_evaluation_only'] is not True or pack['max_people'] != 8 or pack['profile_sha256'] != sha256(bound(root, profile_path)):
        raise ValueError('Selected pack/profile hash contract mismatch')
    if len(pack['models']) != 1: raise ValueError('Expected one YOLO model')
    model = pack['models'][0]
    options = model['backend_options']
    expected_options={'use_packing_layout': True, 'use_subgroup_ops': False, 'use_fp16_packed': False, 'use_fp16_storage': False, 'use_fp16_arithmetic': False}
    if kernel=='sgemm':expected_options.update(use_winograd_convolution=False,use_sgemm_convolution=True)
    if options != expected_options or any(type(value) is not bool for value in options.values()):
        raise ValueError('Expected explicit FP32 backend options')
    contract = model['input_contract']
    height=384 if size==640 else size
    if contract['width'] != size or contract['height'] != height or contract['tensor_dtype'] != 'fp32': raise ValueError('Input contract differs')
    if size==640:
        anchors=5040
        pack_id='yolov8n-pose-rectangle640x384-fp32'+('-sgemm' if kernel=='sgemm' else '')+'-local'
        execution='raw_tensor_fp32_sgemm_v1' if kernel=='sgemm' else 'raw_tensor_fp32_v1'
        if profile['body']['modelPack']!=pack_id or \
            model['decoder_id']!='yolov8_pose_dfl17_v1' or model['execution_contract']!=execution or \
            contract!={'image_format':'rgba8-unorm','color_order':'rgb','crop':'letterbox',
                'resize_interpolation':'bilinear','pad_rgb':[114,114,114],'normalization':{'mean':[0,0,0],'norm':[1/255]*3},
                'width':640,'height':384,'tensor_dtype':'fp32','elempack':1,'input_blob':'in0'} or \
            model['output_contract']!={'decoder':'yolov8_pose_dfl17_v1','output_blobs':['out0','out1'],
                'max_output_bytes':{'out0':anchors*65*4,'out1':anchors*51*4}}:
            raise ValueError('Exact rectangle FP32 tensor contract differs')
    for kind in ('param', 'bin'):
        path = bound(pack_root, model[kind+'_path'])
        relative = path.relative_to(root.resolve()).as_posix()
        if relative not in files or sha256(path) != model[kind+'_sha256']: raise ValueError('Selected model hash closure mismatch')
    return index, files


def stage(runtime, native, video, output, size, kernel='default'):
    runtime=runtime.resolve(); output=output.resolve()
    require_new_output(output)
    runtime_version(size,kernel)
    index_sha=SGEMM_INDEX_SHA if kernel=='sgemm' else INDEX_SHA[size]
    native_sha=SGEMM_NATIVE_SHA if kernel=='sgemm' else NATIVE_SHA
    if sha256(runtime/'index.json') != index_sha: raise ValueError('Reviewed runtime index hash differs')
    index, files = verify_runtime(runtime, size, kernel)
    if kernel=='sgemm':verify_sgemm_identity(index,files)
    if sha256(native) != native_sha: raise ValueError('Reviewed native hash differs')
    if sha256(video) != VIDEO_SHA: raise ValueError('Source video hash differs')
    project=output/'UnityProject'
    source=ROOT/'unity/HumanVisionDemo'
    for relative in ('Assets/HumanVision', 'Assets/Scenes', 'ProjectSettings', 'Packages'):
        shutil.copytree(source/relative, project/relative)
    editor=project/'Assets/HumanVision/Editor'; live=project/'Assets/HumanVision/Demo/Live'
    shutil.copy2(ROOT/'tools/test/TopDownEvalBuild.cs', editor/'TopDownEvalBuild.cs')
    for name in ('TopDownEvalProbe.cs', 'TopDownEvalVideoSource.cs', 'TopDownEvalParitySource.cs'):
        text=(ROOT/'tools/test'/name).read_text(encoding='utf-8-sig')
        text=text.replace('public const int Capacity = 4;', 'public const int Capacity = 8;').replace('public const int Interval = 4;', 'public const int Interval = 2;')
        text=text.replace('video-2.mp4', video.name).replace('REPLACE_VIDEO_SHA256', VIDEO_SHA).replace('public const double StartSeconds = 0;', 'public const double StartSeconds = 37;')
        (live/name).write_text(text, encoding='utf-8', newline='\n')
    shutil.copytree(ROOT/'upm/com.blazetc.humanvision/Runtime/Plugins/Android/HumanVisionPermissions.androidlib', project/'Assets/Plugins/Android/HumanVisionPermissions.androidlib')
    embedded=project/'Assets/StreamingAssets/HumanVision/Runtime'
    for relative in files:
        for target_root in (project, embedded):
            target=target_root/relative; target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(runtime/relative, target)
    generated=unity_index(index,files,size,kernel)
    (embedded/'index.json').write_text(json.dumps(generated, indent=2)+'\n', encoding='utf-8')
    verify_runtime(embedded, size, kernel)
    for relative, digest in files.items():
        if sha256(project/relative) != digest: raise ValueError('Copied root runtime hash differs: '+relative)
    legacy=ROOT/'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
    shutil.copytree(legacy, project/'modelpacks/precision-t-26-ncnn-fp16')
    diagnostic=project/'Assets/StreamingAssets/HumanVision/Diagnostic'/video.name
    diagnostic.parent.mkdir(parents=True); shutil.copy2(video, diagnostic)
    if sha256(diagnostic) != VIDEO_SHA: raise ValueError('Copied video hash differs')
    metadata={'size':size,'capacity':8,'interval_package_id':2,'source_runtime_index_sha256':sha256(runtime/'index.json'),
        'native_sha256':native_sha,'video_sha256':VIDEO_SHA,'video_start_seconds':37,'selected_files':files}
    if kernel=='sgemm':metadata['convolution_kernel']=kernel
    if size==640:metadata.update(shape_id='rectangle640x384',input_width=640,input_height=384,source_aspect_ratio='16:9')
    (output/'stage-manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    return project


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True); parser.add_argument('--native',type=Path)
    parser.add_argument('--video',type=Path); parser.add_argument('--output',type=Path)
    parser.add_argument('--size',type=int,choices=(320,416,640),default=320); parser.add_argument('--verify-only',action='store_true')
    parser.add_argument('--convolution-kernel',choices=('default','sgemm'),default='default')
    args=parser.parse_args()
    if args.verify_only:
        index,files=verify_runtime(args.runtime.resolve(),args.size,args.convolution_kernel)
        if args.convolution_kernel=='sgemm':verify_sgemm_identity(index,files)
        print('Runtime selection/hash closure PASS')
    else:
        if not all((args.native,args.video,args.output)): parser.error('stage requires native, video, output')
        print(stage(args.runtime,args.native,args.video,args.output,args.size,args.convolution_kernel))
