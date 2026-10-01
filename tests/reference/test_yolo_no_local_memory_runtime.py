"""Explicit NO_LOCAL_MEMORY runtime staging using frozen offline evidence; no FPS claim."""
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

class NoLocalMemoryRuntimeTests(unittest.TestCase):
    def test_new_pack_is_explicit_and_hash_closed(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo',prefix='no-local-memory-stage-test-') as directory:
            runtime=Path(directory)/'runtime'
            generate.stage(640,runtime,convolution_kernel='no-local-memory')
            index=json.loads((runtime/'index.json').read_text())
            self.assertEqual(index['convolution_kernel'],'no-local-memory')
            self.assertEqual(index['eligibility_evidence_sha256'],generate.NO_LOCAL_MEMORY_EVIDENCE_SHA)
            self.assertEqual(len(index['eligibility_archives']),11)
            self.assertFalse(index['hardware_fps_acceptance'])
            self.assertEqual(index['execution_contract'],'raw_tensor_fp32_no_local_memory_v1')
            pack=json.loads((runtime/'modelpacks/yolov8n-pose-rectangle640x384-fp32-no-local-memory-local/modelpack.json').read_text())
            self.assertEqual(pack['execution_contract'],index['execution_contract'])
            options=pack['models'][0]['backend_options']
            self.assertEqual(len(options),8)
            self.assertTrue(options['use_winograd_convolution']);self.assertFalse(options['use_shader_local_memory']);self.assertTrue(options['use_sgemm_convolution'])
            for name,digest in index['files'].items():self.assertEqual(generate.sha(runtime/name),digest)
            with self.assertRaises(ValueError):generate.stage(640,runtime,convolution_kernel='no-local-memory')

    def test_kernel_and_unreviewed_shapes_fail_before_writing(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            for size,kernel in [(640,'winograd'),(320,'no-local-memory'),(416,'no-local-memory'),(512,'no-local-memory')]:
                with self.subTest(size=size,kernel=kernel),self.assertRaises(ValueError):
                    generate.stage(size,runtime,convolution_kernel=kernel)
                self.assertFalse(runtime.exists())

    def test_fresh_numerical_gate_failure_prevents_any_pack_write(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            with patch.object(generate,'compare_directory',return_value={'passed':False,'error':'changed raw output'}),self.assertRaises(ValueError):
                generate.stage(640,runtime,convolution_kernel='no-local-memory')
            self.assertFalse(runtime.exists())

    def test_rehashed_index_cannot_replace_reviewed_evidence(self):
        evidence=ROOT/'tools/models/ncnn/yolo_no_local_memory_gate_evidence.json'
        original=generate.sha
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
            runtime=Path(directory)/'runtime'
            with patch.object(generate,'sha',side_effect=lambda p:'0'*64 if Path(p)==evidence else original(p)),self.assertRaises(ValueError):
                generate.stage(640,runtime,convolution_kernel='no-local-memory')
            self.assertFalse(runtime.exists())

    def test_fresh_gate_checks_execution_kernel_and_model_binding(self):
        evidence=json.loads((ROOT/'tools/models/ncnn/yolo_no_local_memory_gate_evidence.json').read_text())
        first=evidence['fixtures'][0]
        for mutation in ('kernel','model','source'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                sandbox=Path(directory)
                run=sandbox/'out'/first['archive']
                shutil.copytree(ROOT/'out'/first['archive'],run)
                oracle=sandbox/'out'/first['historical_cpu_archive'];oracle.mkdir(parents=True)
                for name in first['cpu_output_sha256']:shutil.copyfile(ROOT/'out'/first['historical_cpu_archive']/name,oracle/name)
                execution=run/'execution.json';record=json.loads(execution.read_text())
                if mutation=='kernel':record['stages']['gpu-fp32-no-local-memory']['options']['use_shader_local_memory']=True
                elif mutation=='model':record['model_hashes']['yolov8n_pose.ncnn.bin']='0'*64
                else:record['runner_source_sha256']='0'*64
                execution.write_text(json.dumps(record))
                original_sha=generate.sha
                # Simulate an attacker rehashing archive metadata in the index.
                # The actual gate uses its own sha and still rejects the execution.
                def rehashed(path):
                    return first['execution_sha256'] if Path(path)==execution else original_sha(path)
                with patch.object(generate,'ROOT',sandbox),patch.object(generate,'sha',side_effect=rehashed):
                    with self.assertRaisesRegex(ValueError,'Fresh no-local-memory numerical gate failed'):
                        generate.validate_no_local_memory_evidence()

    def test_archive_log_source_and_recipe_tamper_prevent_any_write(self):
        evidence=json.loads((ROOT/'tools/models/ncnn/yolo_no_local_memory_gate_evidence.json').read_text())
        first=evidence['fixtures'][0]
        targets=[ROOT/'out'/first['archive']/first['stages']['gpu-fp32-no-local-memory']['log_file'],
                 ROOT/'out'/first['archive']/'source.png',
                 ROOT/'tools/models/ncnn/yolo_no_local_memory_golden_runner.cpp',
                 ROOT/'tools/models/ncnn/yolo_no_local_memory_runner/CMakeLists.txt']
        original=generate.sha
        for target in targets:
            with self.subTest(target=target),tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as directory:
                runtime=Path(directory)/'runtime'
                with patch.object(generate,'sha',side_effect=lambda p:'0'*64 if Path(p)==target else original(p)):
                    with self.assertRaises(ValueError):generate.stage(640,runtime,convolution_kernel='no-local-memory')
                self.assertFalse(runtime.exists())

if __name__=='__main__':unittest.main()
