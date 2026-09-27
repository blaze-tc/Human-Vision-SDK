"""Hash-bound offline/model-output analysis; reads only normal detector outputs and scalar pose logs."""
from __future__ import annotations
import argparse,hashlib,json,re
from pathlib import Path
import numpy as np
from tools.models.ncnn.compare_detector_outputs import compare_detector,_iou

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def associate_poses(bodies, regions, width, height):
    """Require a unique body using either independently annotated anatomical region.

    Each midpoint requires both valid joints. A missing/clipped region never
    invents a midpoint; unmatched regions remain explicit evidence.
    """
    associations = []
    used = set()
    def inside(point, rect):
        return rect[0] <= point[0] <= rect[2] and rect[1] <= point[1] <= rect[3]
    for person in regions['people']:
        matches = []
        for index, body in bodies.items():
            joints = {joint['index']: joint for joint in body['joints']}
            points = {}
            matched = []
            for name, indices in [('shoulder', (5,12)), ('pelvis', (18,22))]:
                pair = [joints.get(i) for i in indices]
                points[name] = None
                if all(joint and joint['valid'] for joint in pair):
                    points[name] = [(pair[0][axis]+pair[1][axis])/2 for axis in ('x','y')]
                    if inside(points[name], person[name+'_midpoint_xyxy']):
                        matched.append(name)
            if matched:
                matches.append((index, points, matched))
        if len(matches) != 1 or matches[0][0] in used:
            raise ValueError('Independent anatomical association is missing or ambiguous: '+person['id'])
        index, points, matched = matches[0]
        used.add(index)
        body = bodies[index]
        valid = [joint for joint in body['joints'] if joint['valid']]
        if len(valid) < 5 or not all(joint['timestamp'] == body['timestamp'] and joint['frame'] == body['frame']
            and 0 <= joint['x'] < width and 0 <= joint['y'] < height
            and np.isfinite(joint['confidence']) for joint in valid):
            raise ValueError('Accepted pose contains stale or invalid joint metadata')
        associations.append({'annotation':person['id'], 'body':index, 'shoulder_midpoint':points['shoulder'],
            'pelvis_midpoint':points['pelvis'], 'matched_regions':matched,
            'unmatched_regions':[name for name in ('shoulder','pelvis') if name not in matched],
            'valid_canonical_joints':len(valid),
            'frame':body['frame'], 'timestamp':body['timestamp']})
    return associations

def decode(cls,bbox,manifest):
    p=manifest['preprocessing'];w,h=manifest['width'],manifest['height']
    sx=p['resized_width']/w;sy=p['resized_height']/h;left,top=p['pad_left'],p['pad_top']
    boxes=[]
    for i,raw in enumerate(cls.reshape(-1)):
        score=float(1/(1+np.exp(-raw)))
        if score<.35: continue
        level=0 if i<1600 else 1 if i<2000 else 2
        local=i-(0,1600,2000)[level];grid=(40,20,10)[level];stride=8<<level
        # Pinned MlvlPointGenerator offset0, independently established by export config.
        cx=(local%grid)*stride;cy=(local//grid)*stride;d=bbox.reshape(-1,4)[i]
        box=[float(np.clip((cx-d[0]-left)/sx,0,w)),float(np.clip((cy-d[1]-top)/sy,0,h)),
             float(np.clip((cx+d[2]-left)/sx,0,w)),float(np.clip((cy+d[3]-top)/sy,0,h))]
        if box[2]>box[0] and box[3]>box[1]:boxes.append({'score':score,'bbox':box})
    selected=[]
    for box in sorted(boxes,key=lambda b:-b['score']):
        if all(_iou(box['bbox'],other['bbox'])<=.6 for other in selected):selected.append(box)
        if len(selected)==8:break
    return selected

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--manifest',type=Path,required=True);parser.add_argument('--run',type=Path,required=True);parser.add_argument('--reference',type=Path,required=True);args=parser.parse_args()
    m=json.loads(args.manifest.read_text());r=json.loads((args.run/'report.json').read_text(encoding='utf-8-sig'))
    ref=json.loads((args.reference/'report.json').read_text());result={'task2_pass':False,'manifest_sha256':sha(args.manifest),'device_report_sha256':sha(args.run/'report.json'),'detector_strict_pass':False,'annotated_pose_association_pass':False,'error':None}
    try:
        assert r['manifest_sha256']==sha(args.manifest) and r['identity_verified'] and r['source_active']
        assert ref['input_fp16_sha256']==m['artifacts']['tensor_fp16_rtz']['sha256']
        assert sha(args.reference/'cls.f32')==ref['cls_sha256'] and sha(args.reference/'bbox.f32')==ref['bbox_sha256']
        expected_cls=np.fromfile(args.reference/'cls.f32',np.float32);expected_bbox=np.fromfile(args.reference/'bbox.f32',np.float32)
        expected=decode(expected_cls,expected_bbox,m);sources={}
        for item in r['detector_outputs']:
            file=args.run/item['file'];assert sha(file)==item['sha256'];sources.setdefault(item['source_id'],{})[item['name']]=np.fromfile(file,np.float32)
        assert len(sources)==2 and all(set(v)=={'cls','bbox'} for v in sources.values())
        candidates=[];raw=[]
        for source,values in sources.items():
            candidates.append(decode(values['cls'],values['bbox'],m));row={'source_id':source}
            for name,reference in [('cls',expected_cls),('bbox',expected_bbox)]:
                assert reference.shape==values[name].shape and np.isfinite(values[name]).all()
                delta=np.abs(reference-values[name]);row[name]={'max':float(delta.max()),'mean':float(delta.mean()),'p95':float(np.percentile(delta,95))}
            raw.append(row)
        result.update(raw_detector_errors=raw,raw_error_note='Recorded independently; strict authoritative detector comparison is one-to-one box IoU>=.95 and score error<=.01.',reference_boxes=expected,candidate_boxes=candidates)
        result['detector_comparison']=compare_detector(expected,candidates[0],.35,candidates[1]);result['detector_strict_pass']=True
        log=args.run/'logcat.txt';assert sha(log)==r['logcat_sha256'];text=log.read_text(encoding='utf-8-sig');bodies={}
        number=r'([-+0-9.eE]+)'
        for match in re.finditer(r'pose_sample frame=(\d+) body=(\d+) count=(\d+) confidence='+number+r' box='+','.join([number]*4)+r' timestamp=(\d+)',text):
            frame,body,count,confidence,x,y,w,h,timestamp=match.groups();x,y,w,h=map(float,(x,y,w,h));bodies[int(body)]={'frame':int(frame),'count':int(count),'confidence':float(confidence),'bbox':[x,y,x+w,y+h],'timestamp':int(timestamp),'joints':[]}
        for match in re.finditer(r'pose_joint frame=(\d+) body=(\d+) joint=(\d+) valid=(\d+) x='+number+r' y='+number+r' confidence='+number+r' timestamp=(\d+)',text):
            frame,body,joint,valid,x,y,confidence,timestamp=match.groups();bodies[int(body)]['joints'].append({'index':int(joint),'valid':bool(int(valid)),'x':float(x),'y':float(y),'confidence':float(confidence),'timestamp':int(timestamp),'frame':int(frame)})
        assert bodies,'No integrated canonical pose sample captured'
        result['canonical_pose_samples']=bodies
        assert all(len(b['joints'])==32 and sorted(j['index'] for j in b['joints'])==list(range(32)) for b in bodies.values()), 'Incomplete canonical pose log capture'
        result['manual_box_comparison']=[{'annotation':a['id'],
            'reference_best_iou':max(_iou(a['bbox_xyxy'],b['bbox']) for b in expected),
            'pose_best_iou':max(_iou(a['bbox_xyxy'],b['bbox']) for b in bodies.values())}
            for a in m['annotations']]
        rejected=re.findall(r'pose decode rejected valid17=(\d+) confidence_range=([^ ]+) crop=([^\r\n]+)',text)
        pose_calls=re.findall(r'pose frame=(\d+) track=(\d+) decoded=(\d+) accepted=(\d+) joints=(\d+)',text)
        result['pose_validation_evidence']={'decode_rejections':len(rejected),
            'rejected_valid17_histogram':{v:sum(x[0]==v for x in rejected) for v in sorted({x[0] for x in rejected})},
            'accepted':sum(x[3]=='1' for x in pose_calls),
            'decoded_but_not_accepted':sum(x[2]=='1' and x[3]=='0' for x in pose_calls),
            'first_rejected_crops':rejected[:8]}
        region_path=args.manifest.parent/'independent-body-regions.json'
        regions=json.loads(region_path.read_text())
        assert regions['source_rgba_sha256']==m['artifacts']['rgba']['sha256'] and regions['frame_index']==m['frame_index']
        assert regions['width']==m['width'] and regions['height']==m['height'] and regions['coordinate_origin']=='top_left_pixels'
        assert sorted(p['id'] for p in regions['people'])==sorted(p['id'] for p in m['annotations'])
        associations=associate_poses(bodies,regions,m['width'],m['height'])
        result.update(annotated_pose_association_pass=True, associations=associations,
            anatomical_regions_sha256=sha(region_path), canonical_pose_samples=bodies,
            pose_coverage_note='Unique one-to-one shoulder OR hip midpoint association with independent image-only regions; unmatched regions retained. Low-confidence visible joints remain limitations; this is not all-joint visibility acceptance.')
    except Exception as e:result['error']=str(e)
    output=args.run/'model-analysis.json';output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k in ('detector_strict_pass','annotated_pose_association_pass','error','associations')}))
    if result['error']:raise SystemExit(1)
if __name__=='__main__':main()
