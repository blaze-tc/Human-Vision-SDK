"""Frozen 512 landscape eligibility and fail-before-write runtime staging."""
import json
import shutil
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/models/ncnn'))
import yolo_stage_runtime as generate

class Rectangle512RuntimeTests(unittest.TestCase):
    def test_exact_shape_and_default_fp32_pack(self):
        self.assertEqual(generate.shape_contract(512),('rectangle512x288',512,288,'seven-512'))
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            generate.stage(512,runtime)
            index=json.loads((runtime/'index.json').read_text())
            self.assertEqual(index['eligibility_evidence_sha256'],'fe23f93f39a77ad8fc3f3b78a60dbb728e34749116fcd22478074b5d493dde74')
            self.assertEqual((index['input_width'],index['input_height'],index['source_aspect_ratio']),(512,288,'16:9'))
            self.assertEqual(len(index['eligibility_archives']),2)
            self.assertFalse(index['hardware_fps_acceptance'])
            pack=json.loads((runtime/'modelpacks/yolov8n-pose-rectangle512x288-fp32-local/modelpack.json').read_text())
            self.assertEqual(pack['execution_contract'],'raw_tensor_fp32_v1')
            self.assertEqual(len(pack['models'][0]['backend_options']),5)
            self.assertEqual(pack['models'][0]['output_contract']['max_output_bytes'],dict(out0=3024*65*4,out1=3024*51*4))
            profile=json.loads((runtime/'profiles/android-ncnn-vulkan.json').read_text())
            self.assertFalse(profile['backend']['allow_fallback'])
            for name,digest in index['files'].items():self.assertEqual(generate.sha(runtime/name),digest)

    def test_unreviewed_kernel_or_shape_never_writes(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            for size,kernel in [(512,'sgemm'),(352,'default'),(288,'default')]:
                with self.subTest(size=size),self.assertRaises(ValueError):generate.stage(size,runtime,kernel)
                self.assertFalse(runtime.exists())

    def test_rehashed_index_fails_before_write(self):
        original=generate.sha
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            with patch.object(generate,'sha',side_effect=lambda p:'0'*64 if Path(p).name=='yolo_rectangle512_gate_evidence.json' else original(p)):
                with self.assertRaises(ValueError):generate.stage(512,runtime)
            self.assertFalse(runtime.exists())

    def test_numerical_or_either_cpu_gpu_arm_failure_never_writes(self):
        original=generate.compare_directory
        for mutation in ('numerical','cpu-arm','gpu-arm'):
            def changed(run,mode):
                report=original(run,mode)
                if mutation=='numerical':report['passed']=False
                elif Path(run).parent.name=='seven-512':
                    people=report['reference' if mutation=='cpu-arm' else 'actual']
                    people[0]['joints'][9][1]=10000
                return report
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                runtime=Path(directory)/'runtime'
                with patch.object(generate,'compare_directory',side_effect=changed),self.assertRaises(ValueError):generate.stage(512,runtime)
                self.assertFalse(runtime.exists())

    def test_tampered_archive_output_source_options_limits_fail_before_write(self):
        evidence=json.loads((ROOT/'tools/models/ncnn/yolo_rectangle512_gate_evidence.json').read_text())
        first=evidence['fixtures'][0]
        for mutation in ('output','source','options','limits','runner'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                sandbox=Path(directory);run=sandbox/evidence['archive_base']/first['archive']
                shutil.copytree(ROOT/evidence['archive_base']/first['archive'],run)
                if mutation=='output':(run/'ncnn-gpu-fp32-out0.fp32').write_bytes(b'changed')
                elif mutation=='source':(run/'source.png').write_bytes(b'changed')
                elif mutation=='runner':(run/'runner').write_bytes(b'changed')
                elif mutation=='options':
                    p=run/'execution.json';record=json.loads(p.read_text());record['stages']['gpu-fp32']['options']['fp16_storage']=True;p.write_text(json.dumps(record))
                else:
                    p=run/'fixture.json';record=json.loads(p.read_text());record['limits']['joint_xy']=30;p.write_text(json.dumps(record))
                runtime=sandbox/'out/android-yolo/runtime'
                with patch.object(generate,'ROOT',sandbox),self.assertRaises(ValueError):generate.stage(512,runtime)
                self.assertFalse(runtime.exists())

    def test_legacy_runtime_byte_identities_are_preserved(self):
        cases=[(320,'default','runtime-square320'),(416,'default','runtime-square416'),
               (640,'default','runtime-rectangle640x384-arm-verified'),(640,'sgemm','runtime-rectangle640x384-sgemm-verified')]
        for size,kernel,name in cases:
            with self.subTest(name=name),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                runtime=Path(directory)/'runtime';generate.stage(size,runtime,kernel)
                previous=ROOT/'out/android-yolo'/name
                self.assertEqual((runtime/'index.json').read_bytes(),(previous/'index.json').read_bytes())
                index=json.loads((runtime/'index.json').read_text())
                for path in index['files']:self.assertEqual((runtime/path).read_bytes(),(previous/path).read_bytes())

if __name__=='__main__':unittest.main()
