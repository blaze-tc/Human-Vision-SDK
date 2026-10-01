import unittest
import json
from pathlib import Path
import tempfile
import subprocess
import sys
import numpy as np
from tools.models.ncnn.yolo_pose_gate import geometry, decode, compare, associate_annotations, compare_directory, sha, MODEL_HASHES, execution_options


def unit_execution_record(directory,gpu_mode):
    """Synthetic metadata confined to analytic unit tests, never device evidence."""
    (directory/'runner').write_bytes(b'unit-test runner bytes')
    fixture=json.loads((directory/'fixture.json').read_text())
    tools=Path(__file__).resolve().parents[2]/'tools/models/ncnn'
    record=dict(schema_version=1,state='SUCCESS',gpu_mode=gpu_mode,run_id='analytic-unit-test',serial='unit-device',device_fingerprint='unit-device-fingerprint',started_ns=1,completed_ns=2,runner_sha256=sha(directory/'runner'),runner_source_sha256=sha(tools/'yolo_golden_runner.cpp'),runner_cmake_sha256=sha(tools/'yolo_runner/CMakeLists.txt'),model_hashes=MODEL_HASHES.copy(),fixture_sha256=sha(directory/'fixture.json'),input_sha256=sha(directory/'input.fp32'),geometry=fixture['geometry'],stages={})
    for mode in ('cpu',gpu_mode):
        log=directory/f'android-{mode}.log'; log.write_text('analytic unit test log')
        record['stages'][mode]=dict(state='SUCCESS',mode=mode,run_id=record['run_id'],serial=record['serial'],device_fingerprint=record['device_fingerprint'],runner_sha256=record['runner_sha256'],options=execution_options(mode),exit_code=0,log_file=log.name,log_sha256=sha(log),output_hashes={f'ncnn-{mode}-{name}.fp32':sha(directory/f'ncnn-{mode}-{name}.fp32') for name in ('out0','out1')})
    (directory/'execution.json').write_text(json.dumps(record))
    return record


class YoloPoseGateTests(unittest.TestCase):
    def raw(self, w=32, h=32):
        n = sum((w//s)*(h//s) for s in (8,16,32))
        det = np.full((n,65), -40., np.float32)
        det[:,:64] = 0
        points = np.zeros((n,51), np.float32)
        return det, points

    def test_rectangular_letterbox(self):
        self.assertEqual(geometry(1920,1080,416), dict(width=416,height=256,resized_width=416,resized_height=234,left=0,top=11,scale=416/1920,source_width=1920,source_height=1080))

    def test_square_contract_has_separate_symmetric_padding(self):
        g=geometry(1024,576,416,square=True)
        self.assertEqual((g['width'],g['height'],g['resized_width'],g['resized_height'],g['left'],g['top']),(416,416,416,234,0,91))
        portrait=geometry(218,346,416,square=True)
        self.assertEqual((portrait['width'],portrait['height'],portrait['top']),(416,416,0))
        self.assertEqual(portrait['left'],(416-portrait['resized_width'])//2)

    def test_dfl_grid_keypoints_and_sigmoid(self):
        det, pts = self.raw()
        det[5,64] = 0
        pts[5] = np.tile([.25,.75,0],17)
        result = decode(det,pts,geometry(32,32,32))
        self.assertEqual(len(result),1)
        np.testing.assert_allclose(result[0]['joints'][0],[12,20,.5])
        self.assertEqual(result[0]['box'],[0.,0.,31.,31.])
        self.assertEqual(result[0]['score'],.5)

    def test_stride_offsets(self):
        det, pts = self.raw()
        det[20,64] = 1
        pts[20] = np.tile([.25,.75,0],17)
        np.testing.assert_allclose(decode(det,pts,geometry(32,32,32))[0]['joints'][0],[16,48,.5])

    def test_rectangular_source_restoration_keeps_offimage_joint(self):
        g=geometry(64,32,32)
        det,pts=self.raw(g['width'],g['height'])
        det[0,64]=1; pts[0]=np.tile([.25,.75,0],17)
        # source scale=.5, padding top8; x4/.5=8,y(12-8)/.5=8
        np.testing.assert_allclose(decode(det,pts,g)[0]['joints'][0],[8,8,.5])
        pts[0,1]=-1
        self.assertEqual(decode(det,pts,g)[0]['joints'][0][1],-48)

    def test_dynamic_shapes_and_invalid_geometry(self):
        for target,expected in [(320,1260),(416,2184),(640,5040)]:
            g=geometry(1024,576,target)
            self.assertEqual(sum(g['width']//s*(g['height']//s) for s in (8,16,32)),expected)
        for w,h,t in [(0,20,320),(20,20,0),(20,20,319)]:
            with self.assertRaises(ValueError): geometry(w,h,t)

    def test_nms_keeps_best_overlapping_candidate(self):
        det, pts = self.raw()
        det[0,64],det[1,64] = 1,2
        self.assertEqual(len(decode(det,pts,geometry(32,32,32))),1)

    def test_empty_is_empty(self):
        det, pts = self.raw()
        self.assertEqual(decode(det,pts,geometry(32,32,32)),[])

    def test_nonfinite_and_wrong_shape_rejected(self):
        det, pts = self.raw()
        with self.assertRaises(ValueError): decode(det[:-1],pts,geometry(32,32,32))
        det[0,0] = np.nan
        with self.assertRaises(ValueError): decode(det,pts,geometry(32,32,32))

    def test_gate_rejects_wrong_counts_and_numeric_drift(self):
        det, pts = self.raw()
        det[0,64] = 1
        g = geometry(32,32,32)
        self.assertTrue(compare(det,pts,det,pts,g,1)['passed'])
        self.assertFalse(compare(det,pts,det,pts,g,7)['passed'])
        changed = pts.copy(); changed[0,0] = 1
        self.assertFalse(compare(det,pts,det,changed,g,1)['passed'])

    def test_nonfinite_reference_fails(self):
        det, pts = self.raw(); det[0,64]=1
        broken = det.copy(); broken[1,0]=np.inf
        self.assertFalse(compare(broken,pts,det,pts,geometry(32,32,32),1)['passed'])

    def test_annotation_gate_rejects_duplicate_person_and_missing_person(self):
        annotations=[{'id':'a','bbox_xyxy':[0,0,20,40]}, {'id':'b','bbox_xyxy':[40,0,60,40]}]
        bodies=[{'box':[0,0,20,40]}, {'box':[0,0,20,40]}]
        self.assertFalse(associate_annotations(bodies,annotations)['passed'])
        bodies[1]['box']=[40,0,60,40]
        self.assertTrue(associate_annotations(bodies,annotations)['passed'])

    def test_missing_output_or_tampered_input_is_fail_closed(self):
        with tempfile.TemporaryDirectory() as folder:
            directory=Path(folder)
            self.assertFalse(compare_directory(directory)['passed'])
            np.zeros((3,32,32),np.float32).tofile(directory/'input.fp32')
            record=dict(geometry=geometry(32,32,32),expected_people=0,input_sha256=sha(directory/'input.fp32'))
            (directory/'fixture.json').write_text(json.dumps(record))
            self.assertFalse(compare_directory(directory)['passed'])
            det,pts=self.raw()
            for mode in ('cpu','gpu'):
                det.tofile(directory/f'ncnn-{mode}-out0.fp32'); pts.tofile(directory/f'ncnn-{mode}-out1.fp32')
            self.assertFalse(compare_directory(directory)['passed'])
            unit_execution_record(directory,'gpu')
            self.assertTrue(compare_directory(directory)['passed'])
            # A precision candidate must not consume baseline GPU files.
            self.assertFalse(compare_directory(directory,'gpu-fp32')['passed'])
            for name in ('out0','out1'):
                (directory/f'ncnn-gpu-fp32-{name}.fp32').write_bytes((directory/f'ncnn-gpu-{name}.fp32').read_bytes())
            self.assertFalse(compare_directory(directory,'gpu-fp32')['passed'])
            unit_execution_record(directory,'gpu-fp32')
            self.assertTrue(compare_directory(directory,'gpu-fp32')['passed'])
            self.assertFalse(compare_directory(directory,'cpu')['passed'])
            self.assertFalse(compare_directory(directory,'gpu-unknown')['passed'])
            (directory/'input.fp32').write_bytes(b'bad')
            self.assertFalse(compare_directory(directory)['passed'])

    def test_stale_outputs_cannot_pass_failed_or_mismatched_execution(self):
        with tempfile.TemporaryDirectory() as folder:
            directory=Path(folder)
            np.zeros((3,32,32),np.float32).tofile(directory/'input.fp32')
            (directory/'fixture.json').write_text(json.dumps(dict(geometry=geometry(32,32,32),expected_people=0,input_sha256=sha(directory/'input.fp32'))))
            det,pts=self.raw()
            for mode in ('cpu','gpu-fp32'):
                det.tofile(directory/f'ncnn-{mode}-out0.fp32'); pts.tofile(directory/f'ncnn-{mode}-out1.fp32')
            good=unit_execution_record(directory,'gpu-fp32')
            self.assertTrue(compare_directory(directory,'gpu-fp32')['passed'])
            for change in ('failure','running','mode','model','runner','options','device','output','log'):
                bad=json.loads(json.dumps(good))
                if change=='failure': bad['stages']['gpu-fp32']['exit_code']=9
                if change=='running': bad['state']='RUNNING'
                if change=='mode': bad['gpu_mode']='gpu'
                if change=='model': bad['model_hashes']['yolov8n_pose.ncnn.bin']='wrong'
                if change=='runner': bad['runner_sha256']='wrong'
                if change=='options': bad['stages']['gpu-fp32']['options']['fp16_storage']=True
                if change=='device': bad['stages']['gpu-fp32']['serial']='another-device'
                if change=='output': bad['stages']['gpu-fp32']['output_hashes']['ncnn-gpu-fp32-out0.fp32']='wrong'
                if change=='log': bad['stages']['gpu-fp32']['log_sha256']='wrong'
                (directory/'execution.json').write_text(json.dumps(bad))
                with self.subTest(change=change): self.assertFalse(compare_directory(directory,'gpu-fp32')['passed'])

    def test_malformed_metadata_cli_overwrites_old_pass(self):
        script=Path(__file__).resolve().parents[2]/'tools/models/ncnn/yolo_pose_gate.py'
        with tempfile.TemporaryDirectory() as folder:
            directory=Path(folder)
            np.zeros((3,32,32),np.float32).tofile(directory/'input.fp32')
            fixture=dict(geometry=geometry(32,32,32),expected_people=0,input_sha256=sha(directory/'input.fp32'))
            (directory/'fixture.json').write_text(json.dumps(fixture))
            det,pts=self.raw()
            for mode in ('cpu','gpu-fp32'):
                det.tofile(directory/f'ncnn-{mode}-out0.fp32'); pts.tofile(directory/f'ncnn-{mode}-out1.fp32')
            good=unit_execution_record(directory,'gpu-fp32')
            cases=[('fixture',[]),('fixture',None),('execution',[]),('execution',None),('execution',{**good,'stages':[]}),('execution',{**good,'stages':{'cpu':None}})]
            for target,bad in cases:
                (directory/'fixture.json').write_text(json.dumps(fixture))
                (directory/'execution.json').write_text(json.dumps(good))
                (directory/(target+'.json')).write_text(json.dumps(bad))
                (directory/'comparison-gpu-fp32.json').write_text('{"passed":true}')
                completed=subprocess.run([sys.executable,str(script),str(directory),'--gpu-mode','gpu-fp32'],capture_output=True,text=True)
                with self.subTest(target=target,value=bad):
                    self.assertEqual(completed.returncode,1)
                    saved=json.loads((directory/'comparison-gpu-fp32.json').read_text())
                    self.assertFalse(saved['passed'])
                    self.assertIn('error',saved)


if __name__ == '__main__': unittest.main()
