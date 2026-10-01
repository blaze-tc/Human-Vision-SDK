"""Explicit SGEMM runtime staging using frozen offline evidence; no FPS claim."""
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

class SgemmRuntimeTests(unittest.TestCase):
    def test_new_pack_is_explicit_and_hash_closed(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo',prefix='sgemm-stage-test-') as directory:
            runtime=Path(directory)/'runtime'
            generate.stage(640,runtime,convolution_kernel='sgemm')
            index=json.loads((runtime/'index.json').read_text())
            self.assertEqual(index['convolution_kernel'],'sgemm')
            self.assertEqual(index['eligibility_evidence_sha256'],generate.SGEMM_EVIDENCE_SHA)
            self.assertEqual(len(index['eligibility_archives']),11)
            self.assertFalse(index['hardware_fps_acceptance'])
            self.assertEqual(index['execution_contract'],'raw_tensor_fp32_sgemm_v1')
            pack=json.loads((runtime/'modelpacks/yolov8n-pose-rectangle640x384-fp32-sgemm-local/modelpack.json').read_text())
            self.assertEqual(pack['execution_contract'],index['execution_contract'])
            options=pack['models'][0]['backend_options']
            self.assertEqual(len(options),7)
            self.assertFalse(options['use_winograd_convolution']);self.assertTrue(options['use_sgemm_convolution'])
            for name,digest in index['files'].items():self.assertEqual(generate.sha(runtime/name),digest)
            with self.assertRaises(ValueError):generate.stage(640,runtime,convolution_kernel='sgemm')

    def test_kernel_and_unreviewed_shapes_fail_before_writing(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            for size,kernel in [(640,'winograd'),(320,'sgemm'),(416,'sgemm')]:
                with self.subTest(size=size,kernel=kernel),self.assertRaises(ValueError):
                    generate.stage(size,runtime,convolution_kernel=kernel)
                self.assertFalse(runtime.exists())

    def test_fresh_numerical_gate_failure_prevents_any_pack_write(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            with patch.object(generate,'compare_directory',return_value={'passed':False,'error':'changed raw output'}),self.assertRaises(ValueError):
                generate.stage(640,runtime,convolution_kernel='sgemm')
            self.assertFalse(runtime.exists())

    def test_rehashed_index_cannot_replace_reviewed_evidence(self):
        evidence=ROOT/'tools/models/ncnn/yolo_sgemm_gate_evidence.json'
        original=generate.sha
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            with patch.object(generate,'sha',side_effect=lambda p:'0'*64 if Path(p)==evidence else original(p)),self.assertRaises(ValueError):
                generate.stage(640,runtime,convolution_kernel='sgemm')
            self.assertFalse(runtime.exists())

    def test_fresh_gate_checks_execution_kernel_and_model_binding(self):
        evidence=json.loads((ROOT/'tools/models/ncnn/yolo_sgemm_gate_evidence.json').read_text())
        first=evidence['fixtures'][0]
        for mutation in ('kernel','model','source'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                sandbox=Path(directory)
                run=sandbox/'out'/first['archive']
                shutil.copytree(ROOT/'out'/first['archive'],run)
                oracle=sandbox/'out'/first['historical_cpu_archive'];oracle.mkdir(parents=True)
                for name in first['cpu_output_sha256']:shutil.copyfile(ROOT/'out'/first['historical_cpu_archive']/name,oracle/name)
                execution=run/'execution.json';record=json.loads(execution.read_text())
                if mutation=='kernel':record['stages']['gpu-fp32-sgemm']['options']['use_winograd_convolution']=True
                elif mutation=='model':record['model_hashes']['yolov8n_pose.ncnn.bin']='0'*64
                else:record['runner_source_sha256']='0'*64
                execution.write_text(json.dumps(record))
                original_sha=generate.sha
                # Simulate an attacker rehashing archive metadata in the index.
                # The actual gate uses its own sha and still rejects the execution.
                def rehashed(path):
                    return first['execution_sha256'] if Path(path)==execution else original_sha(path)
                with patch.object(generate,'ROOT',sandbox),patch.object(generate,'sha',side_effect=rehashed):
                    with self.assertRaisesRegex(ValueError,'Fresh SGEMM numerical gate failed'):
                        generate.validate_sgemm_evidence()

if __name__=='__main__':unittest.main()
