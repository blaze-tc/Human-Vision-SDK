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
RECTANGLE512_NATIVE_SHA = 'f8a34538c8892b7bc626a2fa11ef5b0598e3b8c774b6e3cbd57f1f9377a50f4d'
RECTANGLE512_EVIDENCE_SHA = 'fe23f93f39a77ad8fc3f3b78a60dbb728e34749116fcd22478074b5d493dde74'
RECTANGLE512_FIXTURE = 'android-yolo/resolution-feasibility/device-runs/seven-512/gpu-fp32-b39ee1b984a045e0a5d7c2b770191abc'
RECTANGLE512_SELECTED_FILES = {
    'profiles/android-ncnn-vulkan.json':'4f4f0503879a7698950183490539be4ee3bc53443b8df16393391354fcd7730a',
    'modelpacks/yolov8n-pose-rectangle512x288-fp32-local/modelpack.json':'45a7309da7e87b3d19d1fa48b34e78e89f3182fc69269c4e771a4806d621ff95',
    'modelpacks/yolov8n-pose-rectangle512x288-fp32-local/yolov8n_pose.ncnn.param':'908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905',
    'modelpacks/yolov8n-pose-rectangle512x288-fp32-local/yolov8n_pose.ncnn.bin':'6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9'}
RECTANGLE512_PROVENANCE = {
    'runner_sha256':'87b20ad8289eb0b5b449683bb6b096297221195307b1d66b59502cf49b96a14d',
    'runner_source_sha256':'5d9a2b3ae19239d22f66d77fe6bba52206c700c522fed93810ede4a839d37814',
    'runner_cmake_sha256':'531f7fa450bbc2edc4108b9e92a61e49d58a49a8ced03465c554fd19afae0a46'}
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
NO_LOCAL_MEMORY_NATIVE_SHA = '8a1ab8d250c8d214e3fd5b976c4a55043a5de655d760e7688bb604d5470b8b98'
NO_LOCAL_MEMORY_INDEX_SHA = '9e7df19ee235e85061f3703d1dd4a02bc740fddc02c9c29d743c834fa9f78d65'
NO_LOCAL_MEMORY_UNITY_INDEX_SHA = '64e3ea59c79cfdae0430b54b981daeebe630f8751c35d23765c5b7ada6c11f3c'
NO_LOCAL_MEMORY_EVIDENCE_SHA = '211a59e74cfe3d8d0d96b836f0bbe626d844e863e283acc955b2c3dfd7af5680'
NO_LOCAL_MEMORY_FIXTURE = 'android-yolo/device-runs/seven-640/gpu-fp32-no-local-memory-5c7bb015e67f4cef9129dddec5b5dcf9'
NO_LOCAL_MEMORY_SELECTED_FILES = {'modelpacks/yolov8n-pose-rectangle640x384-fp32-no-local-memory-local/modelpack.json': '1991d6051f006b9630dcc55b9eb561b1ab849e12c7082304eb9fa9cc7b35b636', 'modelpacks/yolov8n-pose-rectangle640x384-fp32-no-local-memory-local/yolov8n_pose.ncnn.bin': '6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9', 'modelpacks/yolov8n-pose-rectangle640x384-fp32-no-local-memory-local/yolov8n_pose.ncnn.param': '908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905', 'profiles/android-ncnn-vulkan.json': '09e4635b18ca15eb94d9bd66cd917c0bfc485fae254d13a0623276f85a63ed49'}
NO_LOCAL_MEMORY_PROVENANCE = {
    'runner_sha256':'4cb46cb5ad25bc7e4da97e318dd35a34e6ac30d939f0bf75091041ff86d58537',
    'runner_source_sha256':'62bb3d5e4acdc15709a04f41bee21a2fe78c14e41f7ec835b30dfe71d1f00a02',
    'runner_cmake_sha256':'6fd87d16b50461cdd53597c59e2b7e7d1bcd86e69091e7b4395b79c99f576e70'}

RECTANGLE576_EVIDENCE_SHA = 'c3389a91ff01e4bfe2a293b52b67b0fc21608ba9cb5916ab60786259acd52cee'
RECTANGLE576_FIXTURE = 'android-yolo/rectangle576-eligibility-20261001/device-runs/seven-576/gpu-fp32-f190aa94c8e540bd9fff84857da7e824'
RECTANGLE576_SELECTED_FILES = {'modelpacks/yolov8n-pose-rectangle576x352-fp32-local/modelpack.json': '50cd6fd79354cf9794ac179c1312a84e2cd914c4b8f253b96477f2073375a99d', 'modelpacks/yolov8n-pose-rectangle576x352-fp32-local/yolov8n_pose.ncnn.bin': '6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9', 'modelpacks/yolov8n-pose-rectangle576x352-fp32-local/yolov8n_pose.ncnn.param': '908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905', 'profiles/android-ncnn-vulkan.json': '92c6d39c0c2cb7780df9c311bd6eae55478873c00fc6a5eff0e91e18b0355335'}
RECTANGLE576_PROVENANCE = {'runner_sha256': '87b20ad8289eb0b5b449683bb6b096297221195307b1d66b59502cf49b96a14d', 'runner_source_sha256': '5d9a2b3ae19239d22f66d77fe6bba52206c700c522fed93810ede4a839d37814', 'runner_cmake_sha256': '531f7fa450bbc2edc4108b9e92a61e49d58a49a8ced03465c554fd19afae0a46', 'eligibility_archives': ['android-yolo/rectangle576-eligibility-20261001/device-runs/seven-576/gpu-fp32-f190aa94c8e540bd9fff84857da7e824', 'android-yolo/rectangle576-eligibility-20261001/device-runs/one-576/gpu-fp32-80f4b8a2a43b4289b60b831794c1d295', 'android-yolo/rectangle576-eligibility-20261001/device-runs/empty-576/gpu-fp32-849c33129c66453aa05d2c1187112357'], 'raised_arm_semantic': {'report': 'arm-semantic-gate.json', 'report_sha256': 'b281dc4f4938bf03a364ee4b626cf30968e62c300bf0b13eaff6fa92a33ef92c', 'passed': True, 'reference_raised_arms': 7, 'actual_raised_arms': 7, 'predicate': {'people': 7, 'source_frame': 1500, 'left_shoulder': 5, 'left_wrist': 9, 'joint_confidence_minimum': 0.2, 'all_seven_left_wrists_above_shoulders': True, 'require_joint_inside_source': True}}}
RECTANGLE576_NATIVE_SHA = '60d847e993db2e8a446f5a6807240be95229c695e20a7040db21474cfab5cb42'
RECTANGLE576_UNITY_INDEX_SHA = 'ca90ae5654c63d1f79d16f10e6b07bcde0f49def1905626d53fe9af2add08a95'

VIDEO_SHA = 'e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8'
INDEX_SHA = {576: '9b1c5b0f3b25c593b4b973ae023cdf3d56615f0fdc1084cdbab3f1a4ede7e46f',
             320: '9119eb49528758d2b4d9237406fd98643e2d0c5d624804ee16769bff9d81ba69',
             416: '10454b1a73e4c86e1d6396e3de251ac398176ed24d31a0d09f7dd8969d7f6629',
             512: 'd37a27c67b74ebbf29866a6cce252af8dabc1ae8bcedddd0c2c5fbada5efc317',
             640: '954b0fb5a152672b2242ccc14d49588be74e488000a110bbe7c91a022b143f00'}

def shape_id(size):
    if size not in INDEX_SHA: raise ValueError('Unreviewed YOLO input shape')
    return {512:'rectangle512x288',576:'rectangle576x352',640:'rectangle640x384'}.get(size,f'square{size}')


def runtime_version(size, kernel):
    if kernel not in ('default', 'sgemm', 'no-local-memory') or kernel in ('sgemm','no-local-memory') and size!=640:
        raise ValueError('Unreviewed convolution kernel or shape')
    return f'yolo-local-fp32-{shape_id(size)}' + ('-'+kernel if kernel!='default' else '')


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


def verify_no_local_memory_identity(index, files):
    if files != NO_LOCAL_MEMORY_SELECTED_FILES or any(index.get(key)!=value for key,value in NO_LOCAL_MEMORY_PROVENANCE.items()):
        raise ValueError('Reviewed no-local-memory model/profile/source identity differs')


def verify_reviewed_no_local_memory_runtime(root, index, files):
    verify_no_local_memory_identity(index, files)
    if sha256(root/'index.json') not in (NO_LOCAL_MEMORY_INDEX_SHA,NO_LOCAL_MEMORY_UNITY_INDEX_SHA):
        raise ValueError('Reviewed no-local-memory runtime index hash differs')


def verify_rectangle576_identity(index, files):
    if files != RECTANGLE576_SELECTED_FILES or any(index.get(key)!=value for key,value in RECTANGLE576_PROVENANCE.items()):
        raise ValueError('Reviewed rectangle576 model/profile/source identity differs')


def verify_reviewed_rectangle576_runtime(root,index,files):
    # Recompute the frozen raw/semantic gate immediately before staging too.
    import sys
    model_tools=str(ROOT/'tools/models/ncnn')
    if model_tools not in sys.path:sys.path.insert(0,model_tools)
    from yolo_stage_runtime import validate_rectangle576_evidence
    validate_rectangle576_evidence()
    verify_rectangle576_identity(index,files)
    if sha256(root/'index.json') not in (INDEX_SHA[576],RECTANGLE576_UNITY_INDEX_SHA):
        raise ValueError('Reviewed rectangle576 runtime index hash differs')


def verify_rectangle512_identity(index, files):
    if files != RECTANGLE512_SELECTED_FILES or any(index.get(key)!=value for key,value in RECTANGLE512_PROVENANCE.items()):
        raise ValueError('Reviewed rectangle512 model/profile/source identity differs')


def verify_runtime(root, size, kernel='default'):
    version=runtime_version(size,kernel)
    index = json.loads((root/'index.json').read_text(encoding='utf-8-sig'))
    if index.get('size') != size or index.get('precision') != 'fp32' or index.get('local_evaluation_only') is not True:
        raise ValueError('Runtime eligibility metadata mismatch')
    if index.get('convolution_kernel','default') != kernel:
        raise ValueError('Explicit convolution kernel selection mismatch')
    if kernel=='no-local-memory' and index.get('eligibility_evidence_sha256')!=NO_LOCAL_MEMORY_EVIDENCE_SHA:
        raise ValueError('Reviewed no-local-memory eligibility identity differs')
    if kernel=='sgemm' and index.get('eligibility_evidence_sha256')!=SGEMM_EVIDENCE_SHA:
        raise ValueError('Reviewed SGEMM eligibility identity differs')
    if size==640 and (index.get('shape_id')!='rectangle640x384' or index.get('input_width')!=640 or
        index.get('input_height')!=384 or index.get('source_aspect_ratio')!='16:9' or
        index.get('fixture')!=(NO_LOCAL_MEMORY_FIXTURE if kernel=='no-local-memory' else SGEMM_FIXTURE if kernel=='sgemm' else 'android-yolo/device-runs/seven-640/gpu-fp32-866e333068bf4b058ca0c40176faf897')):
        raise ValueError('Frozen rectangle640x384 source/eligibility identity differs')
    if size==512 and (index.get('shape_id')!='rectangle512x288' or index.get('input_width')!=512 or
        index.get('input_height')!=288 or index.get('source_aspect_ratio')!='16:9' or
        index.get('fixture')!=RECTANGLE512_FIXTURE or index.get('eligibility_evidence_sha256')!=RECTANGLE512_EVIDENCE_SHA):
        raise ValueError('Frozen rectangle512x288 source/eligibility identity differs')
    if size==576 and (index.get('shape_id')!='rectangle576x352' or index.get('input_width')!=576 or
        index.get('input_height')!=352 or index.get('source_aspect_ratio')!='16:9' or
        index.get('fixture')!=RECTANGLE576_FIXTURE or index.get('eligibility_evidence_sha256')!=RECTANGLE576_EVIDENCE_SHA):
        raise ValueError('Frozen rectangle576x352 source/eligibility identity differs')
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
    if size in (512,576) and (pack.get('execution_contract')!='raw_tensor_fp32_v1' or index.get('execution_contract')!='raw_tensor_fp32_v1'):
        raise ValueError('Rectangle512 index/pack/model execution contract mismatch')
    if kernel=='no-local-memory' and (pack.get('execution_contract')!='raw_tensor_fp32_no_local_memory_v1' or index.get('execution_contract')!='raw_tensor_fp32_no_local_memory_v1'):
        raise ValueError('No-local-memory index/pack/model execution contract mismatch')
    if kernel=='sgemm' and (pack.get('execution_contract')!='raw_tensor_fp32_sgemm_v1' or
                           index.get('execution_contract')!='raw_tensor_fp32_sgemm_v1'):
        raise ValueError('SGEMM index/pack/model execution contract mismatch')
    if pack['pack_id'] != profile['body']['modelPack'] or pack['pipeline_id'] != 'pipeline.yolo.pose' or \
        pack['local_evaluation_only'] is not True or pack['max_people'] != 8 or pack['profile_sha256'] != sha256(bound(root, profile_path)):
        raise ValueError('Selected pack/profile hash contract mismatch')
    if len(pack['models']) != 1: raise ValueError('Expected one YOLO model')
    model = pack['models'][0]
    if kernel=='no-local-memory' and (model.get('role')!='body' or model.get('format')!='ncnn'):
        raise ValueError('No-local-memory requires the reviewed ncnn body model role')
    options = model['backend_options']
    expected_options={'use_packing_layout': True, 'use_subgroup_ops': False, 'use_fp16_packed': False, 'use_fp16_storage': False, 'use_fp16_arithmetic': False}
    if kernel=='no-local-memory':expected_options.update(use_winograd_convolution=True,use_sgemm_convolution=True,use_shader_local_memory=False)
    if kernel=='sgemm':expected_options.update(use_winograd_convolution=False,use_sgemm_convolution=True)
    if options != expected_options or any(type(value) is not bool for value in options.values()):
        raise ValueError('Expected explicit FP32 backend options')
    contract = model['input_contract']
    height={512:288,576:352,640:384}.get(size,size)
    if contract['width'] != size or contract['height'] != height or contract['tensor_dtype'] != 'fp32': raise ValueError('Input contract differs')
    if size in (512,576,640):
        anchors={512:3024,576:4158,640:5040}[size]
        pack_id=f'yolov8n-pose-{shape_id(size)}-fp32'+('-'+kernel if kernel!='default' else '')+'-local'
        execution='raw_tensor_fp32_no_local_memory_v1' if kernel=='no-local-memory' else 'raw_tensor_fp32_sgemm_v1' if kernel=='sgemm' else 'raw_tensor_fp32_v1'
        if profile['body']['modelPack']!=pack_id or \
            model['decoder_id']!='yolov8_pose_dfl17_v1' or model['execution_contract']!=execution or \
            contract!={'image_format':'rgba8-unorm','color_order':'rgb','crop':'letterbox',
                'resize_interpolation':'bilinear','pad_rgb':[114,114,114],'normalization':{'mean':[0,0,0],'norm':[1/255]*3},
                'width':size,'height':height,'tensor_dtype':'fp32','elempack':1,'input_blob':'in0'} or \
            model['output_contract']!={'decoder':'yolov8_pose_dfl17_v1','output_blobs':['out0','out1'],
                'max_output_bytes':{'out0':anchors*65*4,'out1':anchors*51*4}}:
            raise ValueError('Exact rectangle FP32 tensor contract differs')
    for kind in ('param', 'bin'):
        path = bound(pack_root, model[kind+'_path'])
        relative = path.relative_to(root.resolve()).as_posix()
        if relative not in files or sha256(path) != model[kind+'_sha256']: raise ValueError('Selected model hash closure mismatch')
    return index, files


def legacy_sha256_index(root):
    """Satisfy the existing editor check with an actual verified legacy file index."""
    manifest=json.loads((root/'modelpack.json').read_text(encoding='utf-8-sig'))
    for model in manifest['models']:
        for kind in ('param','bin'):
            if sha256(bound(root,model[kind+'_path']))!=model[kind+'_sha256']:
                raise ValueError('Legacy validator pack model hash mismatch')
    for record in manifest['evidence']:
        if sha256(bound(root,record['path']))!=record['sha256']:
            raise ValueError('Legacy validator pack evidence hash mismatch')
    files=sorted(p for p in root.rglob('*') if p.is_file() and p.name!='SHA256SUMS.txt')
    return ''.join(sha256(path)+'  '+path.relative_to(root).as_posix()+'\n' for path in files)


def stage(runtime, native, video, output, size, kernel='default'):
    runtime=runtime.resolve(); output=output.resolve()
    require_new_output(output)
    runtime_version(size,kernel)
    index_sha=NO_LOCAL_MEMORY_INDEX_SHA if kernel=='no-local-memory' else SGEMM_INDEX_SHA if kernel=='sgemm' else INDEX_SHA[size]
    native_sha=NO_LOCAL_MEMORY_NATIVE_SHA if kernel=='no-local-memory' else SGEMM_NATIVE_SHA if kernel=='sgemm' else (RECTANGLE576_NATIVE_SHA if size==576 else RECTANGLE512_NATIVE_SHA if size==512 else NATIVE_SHA)
    if sha256(runtime/'index.json') != index_sha: raise ValueError('Reviewed runtime index hash differs')
    index, files = verify_runtime(runtime, size, kernel)
    if kernel=='no-local-memory':verify_no_local_memory_identity(index,files)
    if kernel=='sgemm':verify_sgemm_identity(index,files)
    if size==576:verify_reviewed_rectangle576_runtime(runtime,index,files)
    if size==512:verify_rectangle512_identity(index,files)
    if sha256(native) != native_sha: raise ValueError('Reviewed native hash differs')
    if sha256(video) != VIDEO_SHA: raise ValueError('Source video hash differs')
    legacy=ROOT/'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
    legacy_index=legacy_sha256_index(legacy) if size==576 else None
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
    (embedded/'index.json').write_text(json.dumps(generated, indent=2)+'\n', encoding='utf-8',
                                       newline='\n' if kernel=='no-local-memory' or size==576 else None)
    copied_index,copied_files=verify_runtime(embedded, size, kernel)
    if kernel=='no-local-memory':verify_reviewed_no_local_memory_runtime(embedded,copied_index,copied_files)
    if size==576:verify_reviewed_rectangle576_runtime(embedded,copied_index,copied_files)
    for relative, digest in files.items():
        if sha256(project/relative) != digest: raise ValueError('Copied root runtime hash differs: '+relative)
    legacy=ROOT/'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
    shutil.copytree(legacy, project/'modelpacks/precision-t-26-ncnn-fp16')
    if size==576:
        copied_legacy=project/'modelpacks/precision-t-26-ncnn-fp16'
        if legacy_sha256_index(copied_legacy)!=legacy_index:raise ValueError('Copied legacy validator pack hash differs')
        (copied_legacy/'SHA256SUMS.txt').write_text(legacy_index,encoding='utf-8',newline='\n')
    diagnostic=project/'Assets/StreamingAssets/HumanVision/Diagnostic'/video.name
    diagnostic.parent.mkdir(parents=True); shutil.copy2(video, diagnostic)
    if sha256(diagnostic) != VIDEO_SHA: raise ValueError('Copied video hash differs')
    metadata={'size':size,'capacity':8,'interval_package_id':2,'source_runtime_index_sha256':sha256(runtime/'index.json'),
        'native_sha256':native_sha,'video_sha256':VIDEO_SHA,'video_start_seconds':37,'selected_files':files}
    if kernel!='default':metadata['convolution_kernel']=kernel
    if size in (512,576,640):metadata.update(shape_id=shape_id(size),input_width=size,input_height={512:288,576:352,640:384}[size],source_aspect_ratio='16:9')
    (output/'stage-manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    return project


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True); parser.add_argument('--native',type=Path)
    parser.add_argument('--video',type=Path); parser.add_argument('--output',type=Path)
    parser.add_argument('--size',type=int,choices=(320,416,512,576,640),default=320); parser.add_argument('--verify-only',action='store_true')
    parser.add_argument('--convolution-kernel',choices=('default','sgemm','no-local-memory'),default='default')
    args=parser.parse_args()
    if args.verify_only:
        index,files=verify_runtime(args.runtime.resolve(),args.size,args.convolution_kernel)
        if args.convolution_kernel=='no-local-memory':verify_reviewed_no_local_memory_runtime(args.runtime.resolve(),index,files)
        if args.convolution_kernel=='sgemm':verify_sgemm_identity(index,files)
        if args.size==576:verify_reviewed_rectangle576_runtime(args.runtime.resolve(),index,files)
        if args.size==512:verify_rectangle512_identity(index,files)
        print('Runtime selection/hash closure PASS')
    else:
        if not all((args.native,args.video,args.output)): parser.error('stage requires native, video, output')
        print(stage(args.runtime,args.native,args.video,args.output,args.size,args.convolution_kernel))
