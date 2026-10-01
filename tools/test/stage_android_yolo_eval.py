"""Local-only Unity evaluation staging; never rebuild native or launch Unity."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NATIVE_SHA = 'f3ab016bbe1d5dda83142a0a8f07e1840fe6eecd8dd30bee5896ac5c25568289'
VIDEO_SHA = 'e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8'
INDEX_SHA = {320: '9119eb49528758d2b4d9237406fd98643e2d0c5d624804ee16769bff9d81ba69',
             416: '10454b1a73e4c86e1d6396e3de251ac398176ed24d31a0d09f7dd8969d7f6629'}


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


def unity_index(index, files, size):
    generated=dict(index)
    generated['version']=f'yolo-local-fp32-square{size}'
    generated['files']=[{'path': p, 'sha256': h} for p,h in sorted(files.items())]
    return generated


def verify_runtime(root, size):
    index = json.loads((root/'index.json').read_text(encoding='utf-8-sig'))
    if index.get('size') != size or index.get('precision') != 'fp32' or index.get('local_evaluation_only') is not True:
        raise ValueError('Runtime eligibility metadata mismatch')
    files = index['files']
    if isinstance(files, list):
        if index.get('version') != f'yolo-local-fp32-square{size}': raise ValueError('Unity index version missing or invalid')
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
    if pack['pack_id'] != profile['body']['modelPack'] or pack['pipeline_id'] != 'pipeline.yolo.pose' or \
        pack['local_evaluation_only'] is not True or pack['max_people'] != 8 or pack['profile_sha256'] != sha256(bound(root, profile_path)):
        raise ValueError('Selected pack/profile hash contract mismatch')
    if len(pack['models']) != 1: raise ValueError('Expected one YOLO model')
    model = pack['models'][0]
    options = model['backend_options']
    if options != {'use_packing_layout': True, 'use_subgroup_ops': False, 'use_fp16_packed': False, 'use_fp16_storage': False, 'use_fp16_arithmetic': False}:
        raise ValueError('Expected explicit FP32 backend options')
    contract = model['input_contract']
    if contract['width'] != size or contract['height'] != size or contract['tensor_dtype'] != 'fp32': raise ValueError('Input contract differs')
    for kind in ('param', 'bin'):
        path = bound(pack_root, model[kind+'_path'])
        relative = path.relative_to(root.resolve()).as_posix()
        if relative not in files or sha256(path) != model[kind+'_sha256']: raise ValueError('Selected model hash closure mismatch')
    return index, files


def stage(runtime, native, video, output, size):
    runtime=runtime.resolve(); output=output.resolve()
    require_new_output(output)
    if sha256(runtime/'index.json') != INDEX_SHA[size]: raise ValueError('Reviewed runtime index hash differs')
    index, files = verify_runtime(runtime, size)
    if sha256(native) != NATIVE_SHA: raise ValueError('Reviewed native hash differs')
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
    generated=unity_index(index,files,size)
    (embedded/'index.json').write_text(json.dumps(generated, indent=2)+'\n', encoding='utf-8')
    verify_runtime(embedded, size)
    for relative, digest in files.items():
        if sha256(project/relative) != digest: raise ValueError('Copied root runtime hash differs: '+relative)
    legacy=ROOT/'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
    shutil.copytree(legacy, project/'modelpacks/precision-t-26-ncnn-fp16')
    diagnostic=project/'Assets/StreamingAssets/HumanVision/Diagnostic'/video.name
    diagnostic.parent.mkdir(parents=True); shutil.copy2(video, diagnostic)
    if sha256(diagnostic) != VIDEO_SHA: raise ValueError('Copied video hash differs')
    metadata={'size':size,'capacity':8,'interval_package_id':2,'source_runtime_index_sha256':sha256(runtime/'index.json'),
        'native_sha256':NATIVE_SHA,'video_sha256':VIDEO_SHA,'video_start_seconds':37,'selected_files':files}
    (output/'stage-manifest.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    return project


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True); parser.add_argument('--native',type=Path)
    parser.add_argument('--video',type=Path); parser.add_argument('--output',type=Path)
    parser.add_argument('--size',type=int,choices=(320,416),default=320); parser.add_argument('--verify-only',action='store_true')
    args=parser.parse_args()
    if args.verify_only: verify_runtime(args.runtime.resolve(),args.size); print('Runtime selection/hash closure PASS')
    else:
        if not all((args.native,args.video,args.output)): parser.error('stage requires native, video, output')
        print(stage(args.runtime,args.native,args.video,args.output,args.size))
