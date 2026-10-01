"""Fresh rectangle576 shared-input fixtures; never modifies historical fixtures.

OpenCV tensors are an offline CPU/Vulkan parity oracle, not production AHB input.
"""
from pathlib import Path
import argparse
import json
import shutil
import sys
import cv2
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from yolo_pose_gate import sha, geometry, LIMITS, MODEL_HASHES, REVISION
from yolo_prepare import prepare

NAMES=('seven-576','one-576','empty-576')
SOURCE_HASHES={'seven-576':'017ac2921ed78faff17d125165430bf45eb350861ff80ef5408c3a4b873dad04',
               'one-576':'cce59593686244632e5f1fd2e23da9c72f8c52c3948f28c16b9ba8299b668ad7'}
SEVEN_MANIFEST='7787f25a914b6507ce9a9b4ac1b635800088d2f71887c31534b99b28b199f193'
SEVEN_RGBA='83db08727c2db6aafe2369e71f689428c3e9197176f2d10933a839b141ab9b67'
SEVEN_VIDEO='e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8'
SEVEN_ANNOTATIONS=[{'bbox_xyxy':box,'id':name,'visibility':'full'} for name,box in (
    ('left-rear',[155,182,236,387]),('left-front',[263,189,360,434]),
    ('middle-left-rear',[387,177,463,393]),('center-front',[491,161,625,456]),
    ('middle-right-rear',[608,181,681,391]),('right-front',[731,160,826,431]),('right-rear',[846,167,936,388]))]

def source_pixels(name,source_root):
    if name not in NAMES:raise ValueError('unknown fixture')
    if name=='empty-576':return np.full((576,1024,3),114,np.uint8), {'expected_people':0,'source':'analytic uniform114 negative control'}
    source=source_root/('seven-640' if name=='seven-576' else 'one-640')
    record=json.loads((source/'fixture.json').read_text())
    if record['source_png_sha256']!=SOURCE_HASHES[name] or sha(source/'source.png')!=SOURCE_HASHES[name]:raise ValueError('pinned source PNG mismatch')
    bgr=cv2.imread(str(source/'source.png'))
    if bgr is None:raise ValueError('source PNG decode failed')
    expected_shape=(576,1024,3) if name=='seven-576' else (346,218,3)
    if bgr.shape!=expected_shape:raise ValueError('source geometry mismatch')
    if name=='seven-576':
        manifest_path=source_root.parent/'android-r4/video-1-landscape-frame-1500/manifest.json'
        if sha(manifest_path)!=SEVEN_MANIFEST:raise ValueError('pinned seven manifest mismatch')
        manifest=json.loads(manifest_path.read_text())
        rgba=manifest['artifacts']['rgba'];rgba_path=manifest_path.parent/rgba['file']
        if rgba['sha256']!=SEVEN_RGBA or sha(rgba_path)!=SEVEN_RGBA:raise ValueError('pinned RGBA mismatch')
        if not np.array_equal(cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB),np.fromfile(rgba_path,np.uint8).reshape(rgba['shape'])[:,:,:3]):raise ValueError('original source pixels mismatch')
        if record['annotations']!=manifest['annotations'] or record['source_video_sha256']!=SEVEN_VIDEO or record['sequential_frame_index']!=1500:raise ValueError('pinned seven provenance mismatch')
    else:
        if record['source_video_sha256']!='20806434a5620aca9e6198782d6882beb6fc53f1e5a0725e48abf128b46f2f94' or record['source_ms_requested']!=1000:raise ValueError('pinned one provenance mismatch')
    stale=('geometry','input_sha256','input_bytes','input_dtype','input_layout','resize','opencv','limits','padding_contract')
    metadata={k:v for k,v in record.items() if k not in stale}
    metadata['original_fixture_sha256']=sha(source/'fixture.json')
    return cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB),metadata

def prepare_fixture(name, source_root, directory):
    if name not in NAMES:raise ValueError('unknown fixture')
    if directory.exists():raise ValueError('destination exists; refuse overwrite')
    rgb,metadata=source_pixels(name,source_root)
    metadata.update(fixture=name,target_long_side=576,upstream_revision=REVISION,model_hashes=MODEL_HASHES,
                    geometry_ruling='576x320 cannot contain preserved 576x324 resize; stride32 yields576x352; portrait offline-only',
                    production_gpu_ahb_input_acceptance=False,hardware_fps_acceptance=False)
    record=prepare(rgb,directory,576,metadata)
    if name in SOURCE_HASHES:
        shutil.copyfile(source_root/('seven-640' if name=='seven-576' else 'one-640')/'source.png',directory/'source.png')
        record['source_png_sha256']=sha(directory/'source.png')
    g=record['geometry'];n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    record['raw_shapes']={'out0':[n,65],'out1':[n,51]}
    (directory/'fixture.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    return record

def validate_fixture(directory):
    record=json.loads((directory/'fixture.json').read_text())
    name=record['fixture']
    if name not in NAMES:raise ValueError('unknown fixture')
    w,h=(218,346) if name=='one-576' else (1024,576)
    g=geometry(w,h,576)
    if record['geometry']!=g:raise ValueError('wrong geometry')
    if record['limits']!=LIMITS or record['model_hashes']!=MODEL_HASHES or record['upstream_revision']!=REVISION:raise ValueError('frozen model/limits mismatch')
    n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    if record['raw_shapes']!={'out0':[n,65],'out1':[n,51]}:raise ValueError('dynamic raw shape mismatch')
    if record['expected_people']!={'seven-576':7,'one-576':1,'empty-576':0}[name]:raise ValueError('expected people mismatch')
    if name=='seven-576' and (record.get('annotations')!=SEVEN_ANNOTATIONS or record.get('source_video_sha256')!=SEVEN_VIDEO or record.get('source_rgba_sha256')!=SEVEN_RGBA or record.get('r4_manifest_sha256')!=SEVEN_MANIFEST or record.get('sequential_frame_index')!=1500):raise ValueError('pinned seven provenance mismatch')
    if name=='one-576' and (record.get('source_video_sha256')!='20806434a5620aca9e6198782d6882beb6fc53f1e5a0725e48abf128b46f2f94' or record.get('source_ms_requested')!=1000):raise ValueError('pinned one provenance mismatch')
    if record.get('hardware_fps_acceptance') is not False or record.get('production_gpu_ahb_input_acceptance') is not False:raise ValueError('offline scope mismatch')
    source=directory/'source.png'
    if sha(source)!=record['source_png_sha256']:raise ValueError('source hash mismatch')
    if name in SOURCE_HASHES and sha(source)!=SOURCE_HASHES[name]:raise ValueError('pinned source mismatch')
    bgr=cv2.imread(str(source))
    if bgr is None or bgr.shape!=(h,w,3):raise ValueError('source geometry mismatch')
    if name=='empty-576':
        ok,encoded=cv2.imencode('.png',np.full((h,w,3),114,np.uint8))
        if not ok or encoded.tobytes()!=source.read_bytes():raise ValueError('analytic source mismatch')
    rgb=cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)
    resized=cv2.resize(rgb,(g['resized_width'],g['resized_height']),interpolation=cv2.INTER_LINEAR)
    padded=np.full((g['height'],g['width'],3),114,np.uint8)
    padded[g['top']:g['top']+resized.shape[0],g['left']:g['left']+resized.shape[1]]=resized
    expected=(padded.transpose(2,0,1).astype(np.float32)/np.float32(255)).tobytes()
    input_path=directory/'input.fp32'
    if sha(input_path)!=record['input_sha256'] or input_path.read_bytes()!=expected or record['input_bytes']!=len(expected):raise ValueError('prepared input mismatch')
    return record

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=Path('out/android-yolo'))
    parser.add_argument('--output',type=Path,default=Path('out/android-yolo/rectangle576-eligibility-20261001'))
    args=parser.parse_args()
    if args.output.exists():raise ValueError('output exists; refuse overwrite')
    # Verify all sources before any output is written.
    for name in NAMES:source_pixels(name,args.source_root)
    upstream=args.source_root/'upstream'
    for name,digest in MODEL_HASHES.items():
        if sha(upstream/'app/src/main/assets'/name)!=digest:raise ValueError('pinned model mismatch')
    args.output.mkdir(parents=True)
    for name in NAMES:
        prepare_fixture(name,args.source_root,args.output/name)
        validate_fixture(args.output/name)
    # Existing root-owned device gate expects root/upstream; this junction-free
    # directory alias resolves only the unchanged historical model cache.
    (args.output/'upstream').symlink_to(upstream.resolve(),target_is_directory=True)
    print(json.dumps({'root':str(args.output),'fixtures':NAMES,'gpu_mode':'gpu-fp32'}))

if __name__=='__main__':main()
