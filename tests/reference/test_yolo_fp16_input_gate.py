from pathlib import Path
import tempfile
import json
import subprocess
import sys
import numpy as np
from tools.models.ncnn.yolo_pose_gate import compare_directory,execution_options,sha,MODEL_HASHES
import unittest
from tools.models.ncnn import yolo_pose_gate as gate

MODE='gpu-fp16packed-input16'
OPTIONS=dict(vulkan=True,fp16_storage=False,fp16_packed=True,fp16_arithmetic=False,subgroup=False,use_packing_layout=True,input_elempack=1,input_bits=16,output_elempack=1,output_bits=32,num_threads=2,input_conversion='gpu-fp32-to-fp16-pack1',use_winograd_convolution=True,use_sgemm_convolution=True)
SUFFIX='mode='+MODE+' vulkan=1 storage16=0 packed16=1 arithmetic16=0 subgroup=0 packing=1 threads=2 winograd=1 sgemm=1'
LOG='configured '+SUFFIX+'\neffective '+SUFFIX+'\ncapabilities fp16_packed=1 fp16_storage=1 fp16_arithmetic=1\nlayers=205 unsupported=0 storage16=0 arithmetic16=0\ntransfer in0 dims=3 w=32 h=32 c=3 pack=1 bits=32\ninput in0 dims=3 w=32 h=32 c=3 pack=1 bits=16\noutput out0 dims=2 w=65 h=21 c=1 pack=1 bits=32\noutput out1 dims=2 w=51 h=21 c=1 pack=1 bits=32\n'

class Fp16InputTests(unittest.TestCase):
    def test_explicit_typed_mode_and_distinct_recipe(self):
        self.assertEqual(gate.execution_options(MODE),OPTIONS)
        source,recipe=gate.runner_recipe(MODE)
        self.assertEqual(source.name,'yolo_fp16_input_golden_runner.cpp')
        self.assertEqual(recipe.parent.name,'yolo_fp16_input_runner')
        self.assertTrue(source.is_file()); self.assertTrue(recipe.is_file())
        self.assertEqual(gate.execution_options('cpu')['input_bits'],32)
        self.assertEqual(gate.execution_options('gpu-fp32-packed16')['input_bits'],32)

    def test_actual_typed_log_accepts_supported_device(self):
        self.validate(LOG)
        self.validate(LOG.replace('fp16_storage=1 fp16_arithmetic=1','fp16_storage=0 fp16_arithmetic=0'))

    def validate(self,log):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'runner.log'; path.write_text(log)
            gate.validate_fp16_input_log(path,dict(width=32,height=32))

    def test_dtype_options_capabilities_and_missing_lines_reject(self):
        for before,after in [('transfer in0 dims=3 w=32 h=32 c=3 pack=1 bits=32','transfer in0 dims=3 w=32 h=32 c=3 pack=1 bits=16'),('input in0 dims=3 w=32 h=32 c=3 pack=1 bits=16','input in0 dims=3 w=32 h=32 c=3 pack=1 bits=32'),('storage16=0','storage16=1'),('packed16=1','packed16=0'),('winograd=1','winograd=0'),('sgemm=1','sgemm=0'),('effective mode=','wrong-effective mode='),('fp16_packed=1','fp16_packed=0'),('unsupported=0','unsupported=1'),('output out1 dims=2 w=51 h=21 c=1 pack=1 bits=32',''),('output out0 dims=2 w=65 h=21 c=1 pack=1 bits=32','output out0 dims=2 w=65 h=21 c=1 pack=4 bits=16')]:
            with self.subTest(before=before),self.assertRaises(ValueError): self.validate(LOG.replace(before,after))

    def test_upload_stays_fp32_then_gpu_convert_before_input(self):
        source=gate.runner_recipe(MODE)[0].read_text()
        upload=source.index('cmd.record_upload(input,transferred,upload);')
        for flag in ('use_packing_layout','use_fp16_storage','use_fp16_packed','use_fp16_arithmetic'):
            self.assertIn('upload.'+flag+'=false;',source[:upload])
        convert=source.index('convert_packing(transferred,uploaded,1,2,cmd,net.opt);',upload)
        boundary=source.index('ex.input("in0",uploaded)',convert)
        self.assertIn('uploaded.elembits()!=16',source[convert:boundary])
        self.assertIn('transferred.elembits()!=32',source[upload:convert])
        self.assertNotIn('cast_float32_to_float16',source)

    def test_conversion_rejections_drain_before_tensors_die(self):
        source=gate.runner_recipe(MODE)[0].read_text()
        start=source.index('convert_packing(result,fp32,1,1,cmd,net.opt);')
        end=source.index('cmd.record_download(fp32,output,download);',start)
        guarded=source[start:end]
        for condition in ('fp32.empty()','fp32.dims!=2','fp32.w!=columns','fp32.h!=rows','fp32.c!=1','fp32.elempack!=1','fp32.elembits()!=32'):
            self.assertIn(condition,guarded)
        self.assertLess(guarded.index('cmd.submit_and_wait()'),guarded.index('return 11;'))
        self.assertIn('if (ex.extract(name.c_str(),result,cmd)) {',source)
        self.assertIn('if (cmd.submit_and_wait()) return 10;\n                return 9;',source)

class Fp16InputEvidenceTests(unittest.TestCase):
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
        self.record = dict(schema_version=1,state='SUCCESS',gpu_mode=MODE,run_id='analytic-fp16-input',serial='unit',device_fingerprint='unit',started_ns=1,completed_ns=2,runner_sha256=sha(d/'runner'),runner_source_sha256=sha(tools/'yolo_fp16_input_golden_runner.cpp'),runner_cmake_sha256=sha(tools/'yolo_fp16_input_runner/CMakeLists.txt'),model_hashes=MODEL_HASHES.copy(),fixture_sha256=sha(d/'fixture.json'),input_sha256=sha(d/'input.fp32'),geometry=g,stages={})
        det = np.zeros((21,65),np.float32); det[:,64] = -40
        for mode in ('cpu',MODE):
            det.tofile(d/f'ncnn-{mode}-out0.fp32')
            np.zeros((21,51),np.float32).tofile(d/f'ncnn-{mode}-out1.fp32')
            (d/f'android-{mode}.log').write_text(LOG if mode==MODE else 'analytic CPU reference')
            options = OPTIONS.copy()
            if mode=='cpu':
                options.update(vulkan=False,fp16_packed=False,input_bits=32)
                options.pop('use_winograd_convolution'); options.pop('use_sgemm_convolution'); options.pop('input_conversion')
            self.record['stages'][mode] = dict(state='SUCCESS',mode=mode,run_id='analytic-fp16-input',serial='unit',device_fingerprint='unit',runner_sha256=sha(d/'runner'),options=options,exit_code=0,log_file=f'android-{mode}.log',log_sha256=sha(d/f'android-{mode}.log'),output_hashes={f'ncnn-{mode}-{blob}.fp32':sha(d/f'ncnn-{mode}-{blob}.fp32') for blob in ('out0','out1')})
        self.save()

    def save(self):
        (self.directory/'execution.json').write_text(json.dumps(self.record))

    def test_distinct_mode_accepts_explicit_sgemm_contract(self):
        try: actual=execution_options(MODE)
        except ValueError as error: self.fail(str(error))
        self.assertEqual(actual,OPTIONS)
        self.assertTrue(compare_directory(self.directory,MODE)['passed'])
        self.assertFalse(compare_directory(self.directory,'gpu-fp32')['passed'])

    def test_source_and_recipe_cannot_impersonate_historical_runner(self):
        tools = Path(__file__).resolve().parents[2]/'tools/models/ncnn'
        for field,path in [('runner_source_sha256',tools/'yolo_golden_runner.cpp'),('runner_cmake_sha256',tools/'yolo_runner/CMakeLists.txt')]:
            previous=self.record[field]; self.record[field]=sha(path); self.save()
            with self.subTest(field=field): self.assertFalse(compare_directory(self.directory,MODE)['passed'])
            self.record[field]=previous

    def test_changed_execution_option_is_rejected(self):
        for key,value in [('fp16_storage',True),('fp16_packed',False),('fp16_arithmetic',True),('subgroup',True),('use_packing_layout',False),('input_bits',32),('output_elempack',4),('use_winograd_convolution',False),('use_sgemm_convolution',False)]:
            self.record['stages'][MODE]['options'] = {**OPTIONS,key:value}
            self.save()
            with self.subTest(key=key): self.assertFalse(compare_directory(self.directory,MODE)['passed'])

    def test_bound_wrong_output_byte_count_and_nonfinite_are_rejected(self):
        path=self.directory/f'ncnn-{MODE}-out1.fp32'
        for data in (b'\0'*42, np.full((21,51),np.nan,np.float32).tobytes()):
            path.write_bytes(data)
            self.record['stages'][MODE]['output_hashes'][path.name]=sha(path); self.save()
            self.assertFalse(compare_directory(self.directory,MODE)['passed'])

    def test_sgemm_retains_frozen_raw_error_budget(self):
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
