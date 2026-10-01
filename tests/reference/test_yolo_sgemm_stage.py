import json, tempfile, unittest
from pathlib import Path
from tools.test.stage_android_yolo_eval import (verify_runtime, unity_index, sha256,
    verify_sgemm_identity, SGEMM_SELECTED_FILES, SGEMM_PROVENANCE)

class SgemmStageTests(unittest.TestCase):
    def test_reviewed_identity_rejects_rehashed_replacement_models_and_sources(self):
        verify_sgemm_identity(dict(SGEMM_PROVENANCE),dict(SGEMM_SELECTED_FILES))
        for key in SGEMM_SELECTED_FILES:
            files=dict(SGEMM_SELECTED_FILES);files[key]='0'*64
            with self.assertRaises(ValueError):verify_sgemm_identity(dict(SGEMM_PROVENANCE),files)
        for key in SGEMM_PROVENANCE:
            index=dict(SGEMM_PROVENANCE);index[key]='0'*64
            with self.assertRaises(ValueError):verify_sgemm_identity(index,dict(SGEMM_SELECTED_FILES))

    def test_open_project_checks_same_kernel_before_and_after_copy(self):
        adapter=Path(__file__).resolve().parents[2]/'tools/test/stage_android_yolo_open_project.ps1'
        calls=[line for line in adapter.read_text(encoding='utf-8').splitlines()
               if line.startswith('& py -3 ') and 'stage_android_yolo_eval.py' in line]
        self.assertEqual(len(calls),2)
        for call in calls:
            self.assertIn('--convolution-kernel $kernel',call)

    def fixture(self, root):
        pack_id='yolov8n-pose-rectangle640x384-fp32-sgemm-local'
        pack=root/'modelpacks'/pack_id;pack.mkdir(parents=True)
        profile=root/'profiles/android-ncnn-vulkan.json';profile.parent.mkdir()
        profile.write_text(json.dumps(dict(local_evaluation_only=True,frame_policy='every_frame',
            body=dict(pipeline='pipeline.yolo.pose',modelPack=pack_id),hands=dict(enabled=False),
            backend=dict(preference=['backend.ncnn.vulkan'],allow_fallback=False))))
        (pack/'model.param').write_bytes(b'graph');(pack/'model.bin').write_bytes(b'weights')
        model=dict(execution_contract='raw_tensor_fp32_sgemm_v1',decoder_id='yolov8_pose_dfl17_v1',
            param_path='model.param',param_sha256=sha256(pack/'model.param'),
            bin_path='model.bin',bin_sha256=sha256(pack/'model.bin'),
            backend_options=dict(use_packing_layout=True,use_subgroup_ops=False,use_fp16_packed=False,
                use_fp16_storage=False,use_fp16_arithmetic=False,use_winograd_convolution=False,use_sgemm_convolution=True),
            input_contract=dict(image_format='rgba8-unorm',color_order='rgb',crop='letterbox',resize_interpolation='bilinear',
                pad_rgb=[114,114,114],normalization=dict(mean=[0,0,0],norm=[1/255]*3),width=640,height=384,
                tensor_dtype='fp32',elempack=1,input_blob='in0'),
            output_contract=dict(decoder='yolov8_pose_dfl17_v1',output_blobs=['out0','out1'],
                max_output_bytes=dict(out0=5040*65*4,out1=5040*51*4)))
        manifest=dict(execution_contract='raw_tensor_fp32_sgemm_v1',pack_id=pack_id,pipeline_id='pipeline.yolo.pose',local_evaluation_only=True,max_people=8,
                      profile_sha256=sha256(profile),models=[model])
        (pack/'modelpack.json').write_text(json.dumps(manifest))
        index=dict(execution_contract='raw_tensor_fp32_sgemm_v1',local_evaluation_only=True,precision='fp32',size=640,convolution_kernel='sgemm',
            shape_id='rectangle640x384',input_width=640,input_height=384,source_aspect_ratio='16:9',
            fixture='android-yolo/device-runs/seven-640/gpu-fp32-sgemm-bb9d3dd06afd4311b27bae810c35a9fa',
            eligibility_evidence_sha256='1cb1dec026bdc920371a73bfd6da1abbcb88fa6ae22130e0894838b6e69ab104')
        self.rehash(root,index);return pack,index

    def rehash(self,root,index):
        index['files']={p.relative_to(root).as_posix():sha256(p) for p in root.rglob('*') if p.is_file() and p.name!='index.json'}
        (root/'index.json').write_text(json.dumps(index))

    def test_explicit_sgemm_selection_and_unique_unity_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);_,index=self.fixture(root)
            parsed,files=verify_runtime(root,640,'sgemm')
            generated=unity_index(parsed,files,640,'sgemm')
            self.assertEqual(generated['version'],'yolo-local-fp32-rectangle640x384-sgemm')
            (root/'index.json').write_text(json.dumps(generated))
            verify_runtime(root,640,'sgemm')

    def test_sgemm_is_not_default_selection(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            with self.assertRaises(ValueError):verify_runtime(root,640)

    def test_rehashed_pack_or_index_execution_mismatch_rejected(self):
        for owner in ('pack','index'):
            with self.subTest(owner=owner),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);pack,index=self.fixture(root)
                if owner=='pack':
                    path=pack/'modelpack.json';value=json.loads(path.read_text())
                    value['execution_contract']='raw_tensor_fp32_v1';path.write_text(json.dumps(value))
                else:index['execution_contract']='raw_tensor_fp32_v1'
                self.rehash(root,index)
                with self.assertRaises(ValueError):verify_runtime(root,640,'sgemm')

    def test_rehashed_wrong_options_or_execution_cannot_change_kernel(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);pack,index=self.fixture(root)
            path=pack/'modelpack.json';original=json.loads(path.read_text())
            for key in ('option','execution'):
                value=json.loads(json.dumps(original))
                if key=='option':value['models'][0]['backend_options']['use_winograd_convolution']=True
                else:value['models'][0]['execution_contract']='raw_tensor_fp32_v1'
                path.write_text(json.dumps(value));self.rehash(root,index)
                with self.assertRaises(ValueError):verify_runtime(root,640,'sgemm')

    def test_rehashed_wrong_gate_identity_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);_,index=self.fixture(root)
            index['eligibility_evidence_sha256']='0'*64;self.rehash(root,index)
            with self.assertRaises(ValueError):verify_runtime(root,640,'sgemm')

    def test_rehashed_integer_cannot_impersonate_boolean_option(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);pack,index=self.fixture(root)
            path=pack/'modelpack.json';value=json.loads(path.read_text())
            value['models'][0]['backend_options']['use_winograd_convolution']=0
            path.write_text(json.dumps(value));self.rehash(root,index)
            with self.assertRaises(ValueError):verify_runtime(root,640,'sgemm')

    def test_unreviewed_kernel_or_size_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            for size,kernel in ((416,'sgemm'),(640,'unknown')):
                with self.assertRaises(ValueError):verify_runtime(root,size,kernel)
