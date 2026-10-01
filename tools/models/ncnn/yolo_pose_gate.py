"""Offline decoder/golden gate for pinned nihui YOLOv8n-pose raw outputs.

Equations follow f1ac75e app/src/main/jni/yolov8_pose.cpp. This is research
tooling only; no runtime fallback, model redistribution, or FPS acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import numpy as np

REVISION = 'f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca'
MODEL_HASHES = {'yolov8n_pose.ncnn.param':'908ace8e8d5b99622e0b492ebb18a6b287e647d2df7dff81205e8322752e0905',
                'yolov8n_pose.ncnn.bin':'6128010de189605795a496f3d3f6baa6435a493b31796ceaf400cced07038da9'}
# Frozen before device comparison: raw logits are diagnostic + strict budget,
# source-coordinate semantics have their own independent limits.
LIMITS = dict(raw_max=.2,raw_mean=.01,box_iou=.95,score=.01,joint_xy=3.,joint_score=.01)


def execution_options(mode):
    if mode not in ('cpu','gpu','gpu-fp32','gpu-fp32-packed16'): raise ValueError('unknown execution mode')
    half=mode=='gpu'
    return dict(vulkan=mode!='cpu',fp16_storage=half,fp16_packed=half or mode=='gpu-fp32-packed16',fp16_arithmetic=False,subgroup=False,use_packing_layout=True,input_elempack=1,input_bits=16 if half else 32,output_elempack=1,output_bits=32,num_threads=2)


def runner_recipe(gpu_mode):
    base=Path(__file__).resolve().parent
    if gpu_mode=='gpu-fp32-packed16':
        return base/'yolo_packed16_golden_runner.cpp',base/'yolo_packed16_runner/CMakeLists.txt'
    if gpu_mode in ('gpu','gpu-fp32'):
        return base/'yolo_golden_runner.cpp',base/'yolo_runner/CMakeLists.txt'
    raise ValueError('unknown precision candidate')


def validate_packed16_log(path,g):
    """Check actual runner boundary diagnostics, in addition to requested options."""
    lines=path.read_text(encoding='utf-8').splitlines()
    options='options mode=gpu-fp32-packed16 vulkan=1 storage16=0 packed16=1 arithmetic16=0 subgroup=0 packing=1 threads=2'
    option_lines=[line for line in lines if line.startswith('options ')]
    if option_lines!=[options]: raise ValueError('actual mixed packed storage options mismatch')
    layer_lines=[line for line in lines if line.startswith('layers=')]
    if len(layer_lines)!=1 or not re.fullmatch(r'layers=[1-9][0-9]* unsupported=0 storage16=0 arithmetic16=0',layer_lines[0]): raise ValueError('unsupported or wrong precision Vulkan execution')
    n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    expected=[f"input in0 dims=3 w={g['width']} h={g['height']} c=3 pack=1 bits=32",
              f'output out0 dims=2 w=65 h={n} c=1 pack=1 bits=32',
              f'output out1 dims=2 w=51 h={n} c=1 pack=1 bits=32']
    actual=[line for line in lines if line.startswith(('input ','output '))]
    if actual!=expected: raise ValueError('actual FP32 pack1 input/output contract mismatch')


def validate_execution(directory,gpu_mode,fixture):
    evidence=json.loads((directory/'execution.json').read_text())
    if not isinstance(evidence,dict): raise ValueError('execution metadata must be an object')
    if not isinstance(evidence.get('stages'),dict): raise ValueError('execution stages must be an object')
    if evidence.get('schema_version')!=1 or evidence.get('state')!='SUCCESS' or evidence.get('gpu_mode')!=gpu_mode: raise ValueError('execution is absent, failed, or wrong precision')
    if not evidence.get('serial') or not evidence.get('device_fingerprint') or evidence.get('completed_ns',0)<=evidence.get('started_ns',0): raise ValueError('device identity or completion missing')
    if evidence['runner_sha256']!=sha(directory/'runner'): raise ValueError('runner hash mismatch')
    runner_source,runner_cmake=runner_recipe(gpu_mode)
    if evidence['runner_source_sha256']!=sha(runner_source): raise ValueError('runner source hash mismatch')
    if evidence['runner_cmake_sha256']!=sha(runner_cmake): raise ValueError('runner build recipe hash mismatch')
    if evidence['model_hashes']!=MODEL_HASHES: raise ValueError('model hash mismatch')
    if evidence['fixture_sha256']!=sha(directory/'fixture.json') or evidence['input_sha256']!=sha(directory/'input.fp32') or evidence['geometry']!=fixture['geometry']: raise ValueError('execution fixture mismatch')
    if not evidence.get('run_id'): raise ValueError('missing execution run ID')
    for mode in ('cpu',gpu_mode):
        stage=evidence['stages'][mode]
        if not isinstance(stage,dict): raise ValueError('execution stage must be an object')
        if stage.get('state')!='SUCCESS' or stage.get('exit_code')!=0 or stage.get('mode')!=mode: raise ValueError('runner did not succeed')
        for key in ('run_id','serial','device_fingerprint','runner_sha256'):
            if stage.get(key)!=evidence[key]: raise ValueError('stage execution binding mismatch: '+key)
        if stage.get('options')!=execution_options(mode): raise ValueError('precision/options mismatch')
        expected_log=f'android-{mode}.log'
        if stage.get('log_file')!=expected_log or stage.get('log_sha256')!=sha(directory/expected_log): raise ValueError('execution log hash mismatch')
        if mode=='gpu-fp32-packed16': validate_packed16_log(directory/expected_log,fixture['geometry'])
        expected_names={f'ncnn-{mode}-{blob}.fp32' for blob in ('out0','out1')}
        if not isinstance(stage.get('output_hashes'),dict): raise ValueError('execution output hashes must be an object')
        if set(stage['output_hashes'])!=expected_names: raise ValueError('missing execution output binding')
        for filename,digest in stage['output_hashes'].items():
            if sha(directory/filename)!=digest: raise ValueError('execution output hash mismatch')
    return evidence


def sha(path):
    h=hashlib.sha256()
    with open(path,'rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''): h.update(block)
    return h.hexdigest()


def geometry(w,h,target,square=False):
    if min(w,h,target)<=0 or target%32: raise ValueError('positive source and target multiple32 required')
    scale=target/max(w,h)
    rw,rh=(target,int(h*scale)) if w>h else (int(w*scale),target)
    pw,ph=(target,target) if square else ((rw+31)//32*32,(rh+31)//32*32)
    return dict(width=pw,height=ph,resized_width=rw,resized_height=rh,left=(pw-rw)//2,top=(ph-rh)//2,scale=scale,source_width=w,source_height=h)


def sigmoid(x): return 1/(1+np.exp(-np.clip(x,-80,80)))


def iou(a,b):
    inter=max(0,min(a[2],b[2])-max(a[0],b[0]))*max(0,min(a[3],b[3])-max(a[1],b[1]))
    union=(a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-inter
    return inter/union if union>0 else 0.


def decode(det,points,g,threshold=.25,nms=.45):
    n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
    if det.shape!=(n,65) or points.shape!=(n,51): raise ValueError('raw output shape mismatch')
    if not np.isfinite(det).all() or not np.isfinite(points).all(): raise ValueError('nonfinite raw output')
    proposals=[]; offset=0
    for stride in (8,16,32):
        nx,ny=g['width']//stride,g['height']//stride
        for k in np.flatnonzero(sigmoid(det[offset:offset+nx*ny,64])>=threshold):
            row=offset+int(k); y,x=divmod(int(k),nx)
            logits=det[row,:64].reshape(4,16).astype(np.float64)
            weights=np.exp(logits-logits.max(axis=1,keepdims=True))
            distances=(weights@np.arange(16)/weights.sum(axis=1))*stride
            cx,cy=(x+.5)*stride,(y+.5)*stride
            box=np.array([cx-distances[0],cy-distances[1],cx+distances[2],cy+distances[3]])
            joints=points[row].reshape(17,3).astype(np.float64).copy()
            joints[:,0]=(x+joints[:,0]*2)*stride
            joints[:,1]=(y+joints[:,1]*2)*stride
            joints[:,2]=sigmoid(joints[:,2])
            proposals.append(dict(box=box,score=float(sigmoid(det[row,64])),joints=joints,anchor=row))
        offset+=nx*ny
    selected=[]
    for p in sorted(proposals,key=lambda p:(-p['score'],p['anchor'])):
        if all(iou(p['box'],q['box'])<=nms for q in selected): selected.append(p)
    for p in selected:
        p['box']=(p['box']-np.array([g['left'],g['top'],g['left'],g['top']]))/g['scale']
        p['box'][[0,2]]=np.clip(p['box'][[0,2]],0,g['source_width']-1)
        p['box'][[1,3]]=np.clip(p['box'][[1,3]],0,g['source_height']-1)
        p['joints'][:,:2]=(p['joints'][:,:2]-[g['left'],g['top']])/g['scale']
        p['box']=p['box'].tolist(); p['joints']=p['joints'].tolist()
    return sorted(selected,key=lambda p:-(p['box'][2]-p['box'][0])*(p['box'][3]-p['box'][1]))


def compare(rd,rp,ad,ap,g,expected_count):
    result=dict(passed=False,limits=LIMITS,expected_people=expected_count,raw={},associations=[])
    try:
        reference=decode(rd,rp,g); actual=decode(ad,ap,g)
        result.update(reference=reference,actual=actual)
        ok=len(reference)==len(actual)==expected_count
        for name,r,a in [('out0',rd,ad),('out1',rp,ap)]:
            delta=np.abs(r.astype(np.float64)-a)
            item=dict(max_abs=float(delta.max()),mean_abs=float(delta.mean()))
            result['raw'][name]=item
            ok=ok and item['max_abs']<=LIMITS['raw_max'] and item['mean_abs']<=LIMITS['raw_mean']
        unused=set(range(len(actual)))
        # Highest IoU one-to-one assignment; accepted only if every match passes.
        for r in reference:
            if not unused: ok=False; break
            index=max(unused,key=lambda i:iou(r['box'],actual[i]['box'])); unused.remove(index)
            a=actual[index]; rj,aj=np.array(r['joints']),np.array(a['joints'])
            item=dict(iou=iou(r['box'],a['box']),score_error=abs(r['score']-a['score']),joint_xy_max=float(abs(rj[:,:2]-aj[:,:2]).max()),joint_score_max=float(abs(rj[:,2]-aj[:,2]).max()))
            result['associations'].append(item)
            ok=ok and item['iou']>=LIMITS['box_iou'] and item['score_error']<=LIMITS['score'] and item['joint_xy_max']<=LIMITS['joint_xy'] and item['joint_score_max']<=LIMITS['joint_score']
        result['passed']=bool(ok and not unused)
    except ValueError as e: result['error']=str(e)
    return result


def associate_annotations(bodies,annotations,min_iou=.3):
    """Independent manual people rectangles: unique coverage, no duplicate credit.

    Loose manual rectangles assess person presence, not pixel box accuracy;
    CPU/GPU parity keeps the stricter0.95IoU limit independently.
    """
    available=set(range(len(bodies))); matches=[]
    passed=len(bodies)==len(annotations)
    for annotation in annotations:
        if not available: passed=False; break
        index=max(available,key=lambda i:iou(bodies[i]['box'],annotation['bbox_xyxy']))
        overlap=iou(bodies[index]['box'],annotation['bbox_xyxy'])
        available.remove(index); matches.append(dict(person=annotation['id'],body=index,iou=overlap))
        passed=passed and overlap>=min_iou
    return dict(passed=bool(passed and not available),minimum_iou=min_iou,matches=matches)


def main():
    p=argparse.ArgumentParser(); p.add_argument('directory',type=Path)
    p.add_argument('--gpu-mode',choices=('gpu','gpu-fp32','gpu-fp32-packed16'),default='gpu'); a=p.parse_args()
    report=compare_directory(a.directory,a.gpu_mode)
    filename='comparison.json' if a.gpu_mode=='gpu' else f'comparison-{a.gpu_mode}.json'
    (a.directory/filename).write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('reference','actual')},indent=2))
    raise SystemExit(0 if report['passed'] else 1)


def compare_directory(directory,gpu_mode='gpu'):
    """Always produce a failure result for malformed/missing evidence."""
    try:
        if gpu_mode not in ('gpu','gpu-fp32','gpu-fp32-packed16'): raise ValueError('unknown precision candidate')
        record=json.loads((directory/'fixture.json').read_text())
        if not isinstance(record,dict) or not isinstance(record.get('geometry'),dict): raise ValueError('fixture metadata and geometry must be objects')
        g=record['geometry']
        if sha(directory/'input.fp32')!=record['input_sha256']: raise ValueError('fixture input hash mismatch')
        if (directory/'input.fp32').stat().st_size!=g['width']*g['height']*3*4: raise ValueError('fixture input byte count mismatch')
        evidence=validate_execution(directory,gpu_mode,record)
        n=sum((g['width']//s)*(g['height']//s) for s in (8,16,32))
        arrays=[]; hashes={}
        for mode in ('cpu',gpu_mode):
            for name,width in [('out0',65),('out1',51)]:
                path=directory/f'ncnn-{mode}-{name}.fp32'
                values=np.fromfile(path,np.float32)
                if values.size!=n*width or path.stat().st_size!=n*width*4: raise ValueError('wrong output count: '+str(path))
                arrays.append(values.reshape(n,width)); hashes[path.name]=sha(path)
        report=compare(*arrays,g,record['expected_people'])
        report.update(hashes=hashes,fixture_sha256=sha(directory/'fixture.json'),input_sha256=record['input_sha256'],gpu_mode=gpu_mode)
        report['execution_sha256']=sha(directory/'execution.json')
        report['device_serial']=evidence['serial']; report['run_id']=evidence['run_id']
        if record.get('annotations') and 'reference' in report:
            report['annotation_reference']=associate_annotations(report['reference'],record['annotations'])
            report['annotation_actual']=associate_annotations(report['actual'],record['annotations'])
            report['passed']=report['passed'] and report['annotation_reference']['passed'] and report['annotation_actual']['passed']
        return report
    except (ValueError,OSError,KeyError,TypeError) as e:
        return dict(passed=False,error=str(e),limits=LIMITS)


if __name__=='__main__': main()
