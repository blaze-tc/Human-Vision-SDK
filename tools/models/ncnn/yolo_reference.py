"""Decode CPU/GPU raw outputs and render ignored offline evidence."""
from pathlib import Path
import argparse
import json
import sys
import cv2
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from yolo_pose_gate import decode,associate_annotations,sha

PAIRS=((0,1),(1,3),(0,2),(2,4),(5,6),(5,7),(7,9),(6,8),(8,10),(5,11),(6,12),(11,12),(11,13),(12,14),(13,15),(14,16))


def reference(directory,mode):
    meta=json.loads((directory/'fixture.json').read_text()); g=meta['geometry']
    if sha(directory/'input.fp32')!=meta['input_sha256']: raise ValueError('fixture input hash mismatch')
    n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    raw=[]; hashes={}
    for name,width in [('out0',65),('out1',51)]:
        path=directory/f'ncnn-{mode}-{name}.fp32'
        values=np.fromfile(path,np.float32)
        if values.size!=n*width: raise ValueError('raw output byte count mismatch')
        raw.append(values.reshape(n,width)); hashes[path.name]=sha(path)
    bodies=decode(*raw,g)
    annotation=associate_annotations(bodies,meta['annotations']) if meta.get('annotations') else None
    report={'fixture_sha256':sha(directory/'fixture.json'),'input_sha256':meta['input_sha256'],'raw_shapes':[list(r.shape) for r in raw],'nonfinite':[int((~np.isfinite(r)).sum()) for r in raw],'people':len(bodies),'expected_people':meta['expected_people'],'annotation':annotation,'bodies':bodies,'hashes':hashes}
    (directory/f'{mode}-reference.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    image=cv2.imread(str(directory/'source.png'))
    if image is None or sha(directory/'source.png')!=meta['source_png_sha256']: raise ValueError('source PNG hash mismatch')
    for b in bodies:
        x0,y0,x1,y1=map(round,b['box']); cv2.rectangle(image,(x0,y0),(x1,y1),(0,255,0),2)
        joints=b['joints']
        for a,z in PAIRS:
            if joints[a][2]>=.2 and joints[z][2]>=.2: cv2.line(image,tuple(map(round,joints[a][:2])),tuple(map(round,joints[z][:2])),(255,128,0),2)
        for x,y,score in joints:
            if score>=.2: cv2.circle(image,(round(x),round(y)),3,(0,0,255),-1)
    cv2.imwrite(str(directory/f'{mode}-overlay.png'),image)
    return {k:v for k,v in report.items() if k!='bodies'}


def main():
    p=argparse.ArgumentParser(); p.add_argument('directory',type=Path); p.add_argument('--mode',choices=('cpu','gpu','gpu-fp32'),default='cpu'); a=p.parse_args()
    print(json.dumps(reference(a.directory,a.mode),indent=2))


if __name__=='__main__': main()
