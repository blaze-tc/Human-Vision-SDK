"""Analytic contract tests only; these fixtures are never device evidence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np

from tools.models.ncnn.yolo_pose_gate import compare_directory, execution_options, sha, MODEL_HASHES


MODE = 'gpu-fp32-packed16'
OPTIONS = dict(vulkan=True, fp16_storage=False, fp16_packed=True,
               fp16_arithmetic=False, subgroup=False, use_packing_layout=True,
               input_elempack=1, input_bits=32, output_elempack=1,
               output_bits=32, num_threads=2)
LOG = ('options mode=gpu-fp32-packed16 vulkan=1 storage16=0 packed16=1 arithmetic16=0 subgroup=0 packing=1 threads=2\n'
       'layers=205 unsupported=0 storage16=0 arithmetic16=0\n'
       'input in0 dims=3 w=32 h=32 c=3 pack=1 bits=32\n'
       'output out0 dims=2 w=65 h=21 c=1 pack=1 bits=32\n'
       'output out1 dims=2 w=51 h=21 c=1 pack=1 bits=32\n')


class YoloPacked16GateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        d = self.directory
        np.zeros((3,32,32),np.float32).tofile(d/'input.fp32')
        g = dict(width=32,height=32,resized_width=32,resized_height=32,
                 left=0,top=0,scale=1.,source_width=32,source_height=32)
        (d/'fixture.json').write_text(json.dumps(dict(geometry=g,expected_people=0,input_sha256=sha(d/'input.fp32'))))
        (d/'runner').write_bytes(b'analytic-unit-test-only')
        tools = Path(__file__).resolve().parents[2]/'tools/models/ncnn'
        self.record = dict(schema_version=1,state='SUCCESS',gpu_mode=MODE,run_id='analytic-packed16',serial='unit',device_fingerprint='unit',started_ns=1,completed_ns=2,runner_sha256=sha(d/'runner'),runner_source_sha256=sha(tools/'yolo_packed16_golden_runner.cpp'),runner_cmake_sha256=sha(tools/'yolo_packed16_runner/CMakeLists.txt'),model_hashes=MODEL_HASHES.copy(),fixture_sha256=sha(d/'fixture.json'),input_sha256=sha(d/'input.fp32'),geometry=g,stages={})
        det = np.zeros((21,65),np.float32); det[:,64] = -40
        for mode in ('cpu',MODE):
            det.tofile(d/f'ncnn-{mode}-out0.fp32')
            np.zeros((21,51),np.float32).tofile(d/f'ncnn-{mode}-out1.fp32')
            (d/f'android-{mode}.log').write_text(LOG if mode==MODE else 'analytic CPU reference')
            options = OPTIONS.copy()
            if mode=='cpu': options.update(vulkan=False,fp16_packed=False)
            self.record['stages'][mode] = dict(state='SUCCESS',mode=mode,run_id='analytic-packed16',serial='unit',device_fingerprint='unit',runner_sha256=sha(d/'runner'),options=options,exit_code=0,log_file=f'android-{mode}.log',log_sha256=sha(d/f'android-{mode}.log'),output_hashes={f'ncnn-{mode}-{blob}.fp32':sha(d/f'ncnn-{mode}-{blob}.fp32') for blob in ('out0','out1')})
        self.save()

    def save(self):
        (self.directory/'execution.json').write_text(json.dumps(self.record))

    def test_distinct_mode_accepts_explicit_mixed_storage_contract(self):
        try: actual=execution_options(MODE)
        except ValueError as error: self.fail(str(error))
        self.assertEqual(actual,OPTIONS)
        self.assertTrue(compare_directory(self.directory,MODE)['passed'])
        self.assertFalse(compare_directory(self.directory,'gpu-fp32')['passed'])

    def test_changed_execution_option_is_rejected(self):
        for key,value in [('fp16_storage',True),('fp16_packed',False),('fp16_arithmetic',True),('subgroup',True),('use_packing_layout',False),('input_bits',16),('output_elempack',4)]:
            self.record['stages'][MODE]['options'] = {**OPTIONS,key:value}
            self.save()
            with self.subTest(key=key): self.assertFalse(compare_directory(self.directory,MODE)['passed'])

    def test_hash_bound_runner_log_rejects_wrong_options_input_or_output(self):
        for before,after in [('packed16=1','packed16=0'),('input in0 dims=3 w=32 h=32 c=3 pack=1 bits=32','input in0 dims=3 w=32 h=32 c=3 pack=1 bits=16'),('output out0 dims=2 w=65 h=21 c=1 pack=1 bits=32','output out0 dims=2 w=65 h=21 c=1 pack=4 bits=16'),('unsupported=0','unsupported=1'),('output out1 dims=2 w=51 h=21 c=1 pack=1 bits=32','')]:
            log=self.directory/f'android-{MODE}.log'; log.write_text(LOG.replace(before,after))
            self.record['stages'][MODE]['log_sha256']=sha(log); self.save()
            with self.subTest(change=before): self.assertFalse(compare_directory(self.directory,MODE)['passed'])

    def test_bound_wrong_output_byte_count_and_nonfinite_are_rejected(self):
        path=self.directory/f'ncnn-{MODE}-out1.fp32'
        for data in (b'\0'*42, np.full((21,51),np.nan,np.float32).tobytes()):
            path.write_bytes(data)
            self.record['stages'][MODE]['output_hashes'][path.name]=sha(path); self.save()
            self.assertFalse(compare_directory(self.directory,MODE)['passed'])

    def test_mixed_storage_retains_frozen_raw_error_budget(self):
        path=self.directory/f'ncnn-{MODE}-out1.fp32'
        values=np.zeros((21,51),np.float32); values[0,0]=.201; values.tofile(path)
        self.record['stages'][MODE]['output_hashes'][path.name]=sha(path); self.save()
        report=compare_directory(self.directory,MODE)
        self.assertFalse(report['passed'])
        self.assertEqual(report['limits']['raw_max'],.2)

    def test_cli_accepts_mode_and_writes_distinct_report(self):
        script=Path(__file__).resolve().parents[2]/'tools/models/ncnn/yolo_pose_gate.py'
        result=subprocess.run([sys.executable,str(script),str(self.directory),'--gpu-mode',MODE],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(json.loads((self.directory/f'comparison-{MODE}.json').read_text())['passed'])


if __name__=='__main__': unittest.main()
