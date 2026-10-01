"""Prepare ignored same-input CPU/GPU fixtures; pin source and model provenance."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import cv2
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from yolo_pose_gate import REVISION,MODEL_HASHES,LIMITS,sha,geometry


def prepare(rgb,directory,target,metadata,square=False):
    h,w=rgb.shape[:2]; g=geometry(w,h,target,square=square)
    # An offline shared tensor oracle. OpenCV resize is explicitly documented,
    # not claimed byte-identical to ncnn::from_pixels_resize/production GPU.
    resized=cv2.resize(rgb,(g['resized_width'],g['resized_height']),interpolation=cv2.INTER_LINEAR)
    padded=np.full((g['height'],g['width'],3),114,np.uint8)
    padded[g['top']:g['top']+resized.shape[0],g['left']:g['left']+resized.shape[1]]=resized
    tensor=padded.transpose(2,0,1).astype(np.float32)/np.float32(255)
    directory.mkdir(parents=True,exist_ok=True); tensor.tofile(directory/'input.fp32')
    cv2.imwrite(str(directory/'source.png'),cv2.cvtColor(rgb,cv2.COLOR_RGB2BGR))
    cv2.imwrite(str(directory/'prepared.png'),cv2.cvtColor(padded,cv2.COLOR_RGB2BGR))
    record={**metadata,'geometry':g,'padding_contract':'fixed_square' if square else 'upstream_rectangle','input_sha256':sha(directory/'input.fp32'),'input_dtype':'float32','input_layout':'CHW RGB /255','input_bytes':tensor.nbytes,'resize':'OpenCV INTER_LINEAR uint8; shared offline oracle','opencv':cv2.__version__,'source_png_sha256':sha(directory/'source.png'),'limits':LIMITS}
    (directory/'fixture.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def main():
    p=argparse.ArgumentParser(); p.add_argument('--directory',type=Path,default=Path('out/android-yolo'))
    p.add_argument('--squares-only','--square416-only',dest='square416_only',action='store_true'); a=p.parse_args()
    if a.square416_only:
        # Preserve every existing fixture and device golden; copy only verified
        # original RGB pixels into separately named square-contract fixtures.
        for name in ('seven','one'):
            source=a.directory/f'{name}-416'; metadata=json.loads((source/'fixture.json').read_text())
            if sha(source/'source.png')!=metadata['source_png_sha256']: raise ValueError('source PNG hash mismatch')
            image=cv2.imread(str(source/'source.png'))
            if image is None: raise ValueError('source PNG decode failed')
            for size in (320,416):
                metadata['fixture']=name+f'-square{size}'
                prepare(cv2.cvtColor(image,cv2.COLOR_BGR2RGB),a.directory/f'{name}-square{size}',size,metadata,square=True)
        print('Created separate seven/one-square320/416 fixtures; existing fixtures preserved.')
        return
    upstream=a.directory/'upstream'
    head=subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()
    if head!=REVISION: raise ValueError('upstream revision mismatch')
    if subprocess.check_output(['git','-C',str(upstream),'status','--porcelain'],text=True).strip(): raise ValueError('upstream must be clean')
    models=upstream/'app/src/main/assets'
    for name,digest in MODEL_HASHES.items():
        if sha(models/name)!=digest: raise ValueError('model hash mismatch '+name)
    decoder=upstream/'app/src/main/jni/yolov8_pose.cpp'
    provenance={'repository':'https://github.com/nihui/ncnn-android-yolov8','revision':head,'model_hashes':MODEL_HASHES,'decoder_sha256':sha(decoder),'repository_license_file':False,'decoder_license':'BSD-3-Clause notice in yolov8_pose.cpp','weights_license':'Ultralytics origin; redistribution/commercial rights not established; local evaluation only','dynamic_sizes':'upstream export comment uses640 and320; device runs must verify actual shapes','local_evaluation_only':True}
    (a.directory/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    # Reuse exact sequentially-decoded frame1500 with immutable source hash.
    source=Path('out/android-r4/video-1-landscape-frame-1500')
    manifest=json.loads((source/'manifest.json').read_text())
    rgba=manifest['artifacts']['rgba']
    if sha(source/rgba['file'])!=rgba['sha256']: raise ValueError('R4 source RGBA mismatch')
    image=np.fromfile(source/rgba['file'],np.uint8).reshape(rgba['shape'])[:,:,:3]
    metadata={'fixture':'seven','expected_people':7,'r4_manifest_sha256':sha(source/'manifest.json'),'source_rgba_sha256':rgba['sha256'],'source_video':'E:/Project/Human Vision SDK/video-1.mp4','source_video_sha256':'e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8','sequential_frame_index':1500,'annotations':manifest['annotations']}
    records=[]
    for size in (416,320,640): records.append(prepare(image,a.directory/f'seven-{size}',size,metadata))
    # Existing fixture source, same1000ms frame selection as RTMO eligibility.
    video=Path('tests/testdata/d0_3_one_person.mp4')
    cap=cv2.VideoCapture(str(video)); cap.set(cv2.CAP_PROP_POS_MSEC,1000); ok,bgr=cap.read(); cap.release()
    if not ok: raise ValueError('one-person fixture decode failed')
    one={'fixture':'one','expected_people':1,'source_video':str(video),'source_video_sha256':sha(video),'source_ms_requested':1000,'decoder':'OpenCV FFMPEG seek same existing RTMO fixture convention'}
    for size in (416,320,640): records.append(prepare(cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB),a.directory/f'one-{size}',size,one))
    prepare(np.full((256,416,3),114,np.uint8),a.directory/'empty-416',416,{'fixture':'empty','expected_people':0,'source':'analytic uniform114 negative control'})
    print(json.dumps([{'fixture':r['fixture'],'width':r['geometry']['width'],'height':r['geometry']['height'],'hash':r['input_sha256']} for r in records],indent=2))


if __name__=='__main__': main()
