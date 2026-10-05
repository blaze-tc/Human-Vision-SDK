"""Fresh rectangle960 shared-input fixtures; never modifies historical fixtures.

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
from yolo_pose_gate import sha, LIMITS, MODEL_HASHES, REVISION
from yolo_rectangle576_prepare import source_pixels as original_pixels, SEVEN_ANNOTATIONS, SEVEN_MANIFEST, SEVEN_RGBA, SEVEN_VIDEO

NAMES=('seven-960','one-960','empty-960')
SOURCE_HASHES={'seven-960':'017ac2921ed78faff17d125165430bf45eb350861ff80ef5408c3a4b873dad04',
               'one-960':'cce59593686244632e5f1fd2e23da9c72f8c52c3948f28c16b9ba8299b668ad7'}
CANVAS={'width':1024,'height':576,'left':403,'top':115,'source_width':218,'source_height':346,'pad_rgb':[114,114,114]}

def sha_bytes(value):
    import hashlib
    return hashlib.sha256(value).hexdigest()

def geometry():
    # This approved fixed contract deliberately pads to576, rather than the
    # upstream minimal stride32 height544. No stretch or crop.
    return dict(width=960,height=576,resized_width=960,resized_height=540,
                left=0,top=18,scale=960/1024,source_width=1024,source_height=576)

def source_pixels(name,source_root):
    if name not in NAMES:raise ValueError('unknown fixture')
    rgb,metadata=original_pixels(name.replace('-960','-576'),source_root)
    if name=='one-960':
        canvas=np.full((576,1024,3),114,np.uint8)
        canvas[115:461,403:621]=rgb
        metadata.update(original_source_png_sha256=SOURCE_HASHES[name],derived_canvas=CANVAS,
                        source_qualification='offline portrait-derived114 canvas; original person pixels unchanged')
        rgb=canvas
    return rgb,metadata

def prepare(rgb,directory,target,metadata):
    g=geometry()
    resized=cv2.resize(rgb,(960,540),interpolation=cv2.INTER_LINEAR)
    padded=np.full((576,960,3),114,np.uint8);padded[18:558]=resized
    tensor=padded.transpose(2,0,1).astype(np.float32)/np.float32(255)
    directory.mkdir(parents=True);tensor.tofile(directory/'input.fp32')
    cv2.imwrite(str(directory/'source.png'),cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR))
    cv2.imwrite(str(directory/'prepared.png'),cv2.cvtColor(padded,cv2.COLOR_RGB2BGR))
    return {**metadata,'geometry':g,'padding_contract':'fixed_rectangle960x576','input_sha256':sha(directory/'input.fp32'),
            'input_dtype':'float32','input_layout':'CHW RGB /255','input_bytes':tensor.nbytes,
            'resize':'OpenCV INTER_LINEAR uint8; shared offline oracle','opencv':cv2.__version__,
            'source_png_sha256':sha(directory/'source.png'),'limits':LIMITS}

def prepare_fixture(name, source_root, directory):
    if name not in NAMES:raise ValueError('unknown fixture')
    if directory.exists():raise ValueError('destination exists; refuse overwrite')
    rgb,metadata=source_pixels(name,source_root)
    metadata.update(fixture=name,target_long_side=960,upstream_revision=REVISION,model_hashes=MODEL_HASHES,
                    geometry_ruling='960x540 aspect-preserving resize pads18px top/bottom to960x576;derived16:9 one-person canvas offline-only',
                    production_gpu_ahb_input_acceptance=False,hardware_fps_acceptance=False)
    record=prepare(rgb,directory,960,metadata)
    if name=='seven-960':
        shutil.copyfile(source_root/'seven-640'/'source.png',directory/'source.png')
        record['source_png_sha256']=sha(directory/'source.png')
    g=record['geometry'];n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    record['raw_shapes']={'out0':[n,65],'out1':[n,51]}
    (directory/'fixture.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    return record

def validate_fixture(directory):
    record=json.loads((directory/'fixture.json').read_text())
    name=record['fixture']
    if name not in NAMES:raise ValueError('unknown fixture')
    w,h=1024,576
    g=geometry()
    if record['geometry']!=g:raise ValueError('wrong geometry')
    if record['limits']!=LIMITS or record['model_hashes']!=MODEL_HASHES or record['upstream_revision']!=REVISION:raise ValueError('frozen model/limits mismatch')
    n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    if record['raw_shapes']!={'out0':[n,65],'out1':[n,51]}:raise ValueError('dynamic raw shape mismatch')
    if record['expected_people']!={'seven-960':7,'one-960':1,'empty-960':0}[name]:raise ValueError('expected people mismatch')
    if name=='seven-960' and (record.get('annotations')!=SEVEN_ANNOTATIONS or record.get('source_video_sha256')!=SEVEN_VIDEO or record.get('source_rgba_sha256')!=SEVEN_RGBA or record.get('r4_manifest_sha256')!=SEVEN_MANIFEST or record.get('sequential_frame_index')!=1500):raise ValueError('pinned seven provenance mismatch')
    if name=='one-960' and (record.get('source_video_sha256')!='20806434a5620aca9e6198782d6882beb6fc53f1e5a0725e48abf128b46f2f94' or record.get('source_ms_requested')!=1000):raise ValueError('pinned one provenance mismatch')
    if record.get('hardware_fps_acceptance') is not False or record.get('production_gpu_ahb_input_acceptance') is not False:raise ValueError('offline scope mismatch')
    source=directory/'source.png'
    if sha(source)!=record['source_png_sha256']:raise ValueError('source hash mismatch')
    if name=='seven-960' and sha(source)!=SOURCE_HASHES[name]:raise ValueError('pinned source mismatch')
    bgr=cv2.imread(str(source))
    if bgr is None or bgr.shape!=(h,w,3):raise ValueError('source geometry mismatch')
    if name=='one-960':
        if record.get('derived_canvas')!=CANVAS or record.get('original_source_png_sha256')!=SOURCE_HASHES[name]:raise ValueError('derived canvas provenance mismatch')
        original=bgr[115:461,403:621]
        ok,encoded=cv2.imencode('.png',original)
        if not ok or sha_bytes(encoded.tobytes())!=SOURCE_HASHES[name]:raise ValueError('pinned original canvas pixels mismatch')
        expected_canvas=np.full((576,1024,3),114,np.uint8);expected_canvas[115:461,403:621]=original
        if not np.array_equal(bgr,expected_canvas):raise ValueError('derived canvas pixels mismatch')
    if name=='empty-960':
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
    parser.add_argument('--output',type=Path,default=Path('out/android-yolo/rectangle960-eligibility-20261005'))
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
