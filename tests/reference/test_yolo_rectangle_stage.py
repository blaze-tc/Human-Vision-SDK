"""Real M1 archive staging and strict rectangular shape selection regressions."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/models/ncnn'))
import yolo_stage_runtime as generate
from tools.test import stage_android_yolo_eval as selection

class RectangleStageTests(unittest.TestCase):
    def test_exact_eligible_shape_and_frozen_hash_closure(self):
        self.assertEqual(generate.shape_contract(640),('rectangle640x384',640,384,'seven-640'))
        with self.assertRaises(ValueError):generate.shape_contract(608)
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo',prefix='rectangle-stage-test-') as directory:
            runtime=Path(directory)/'runtime'
            generate.stage(640,runtime)
            self.assertEqual(selection.sha256(runtime/'index.json'),selection.INDEX_SHA[640])
            index,files=selection.verify_runtime(runtime,640)
            self.assertEqual(index['shape_id'],'rectangle640x384')
            with self.assertRaises(ValueError):generate.stage(640,runtime)
            embedded=Path(directory)/'embedded'
            shutil.copytree(runtime,embedded)
            (embedded/'index.json').write_text(json.dumps(selection.unity_index(index,files,640)))
            selection.verify_runtime(embedded,640)

    def test_rectangular_identity_cannot_admit_square640_or_precision_drift(self):
        source=ROOT/'out/android-yolo/runtime-rectangle640x384-arm-verified'
        for mutation in ('height','shape_id','source_aspect_ratio','fixture','fp16','anchor_bytes','pad','norm'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as directory:
                runtime=Path(directory)/'runtime';shutil.copytree(source,runtime)
                index=json.loads((runtime/'index.json').read_text())
                pack_path=runtime/'modelpacks/yolov8n-pose-rectangle640x384-fp32-local/modelpack.json'
                pack=json.loads(pack_path.read_text());model=pack['models'][0]
                if mutation=='height':model['input_contract']['height']=640
                elif mutation=='fp16':model['backend_options']['use_fp16_storage']=True
                elif mutation=='anchor_bytes':model['output_contract']['max_output_bytes']['out0']=8400*65*4
                elif mutation=='pad':model['input_contract']['pad_rgb'][0]=0
                elif mutation=='norm':model['input_contract']['normalization']['norm'][0]=1
                else:index[mutation]='unreviewed'
                pack_path.write_text(json.dumps(pack))
                index['files'][pack_path.relative_to(runtime).as_posix()]=selection.sha256(pack_path)
                (runtime/'index.json').write_text(json.dumps(index))
                with self.assertRaises(ValueError):selection.verify_runtime(runtime,640)

if __name__=='__main__':unittest.main()
