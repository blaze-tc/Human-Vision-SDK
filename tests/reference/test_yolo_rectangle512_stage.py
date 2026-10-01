import json,tempfile,unittest
from pathlib import Path
from tests.reference import test_yolo_sgemm_stage as sgemm_fixture
from tools.test.stage_android_yolo_eval import (verify_runtime,unity_index,sha256,
    verify_rectangle512_identity,RECTANGLE512_SELECTED_FILES,RECTANGLE512_PROVENANCE)

class Rectangle512StageTests(unittest.TestCase):
    def test_reviewed_model_and_source_identity_reject_rehashed_tampering(self):
        verify_rectangle512_identity(dict(RECTANGLE512_PROVENANCE),dict(RECTANGLE512_SELECTED_FILES))
        for key in RECTANGLE512_SELECTED_FILES:
            files=dict(RECTANGLE512_SELECTED_FILES);files[key]='0'*64
            with self.assertRaises(ValueError):verify_rectangle512_identity(dict(RECTANGLE512_PROVENANCE),files)
        for key in RECTANGLE512_PROVENANCE:
            index=dict(RECTANGLE512_PROVENANCE);index[key]='0'*64
            with self.assertRaises(ValueError):verify_rectangle512_identity(index,dict(RECTANGLE512_SELECTED_FILES))

    def fixture(self,root):
        helper=sgemm_fixture.SgemmStageTests();pack,index=helper.fixture(root)
        new='yolov8n-pose-rectangle512x288-fp32-local'
        target=pack.with_name(new);pack.rename(target)
        profile=root/'profiles/android-ncnn-vulkan.json'
        value=json.loads(profile.read_text());value['body']['modelPack']=new;profile.write_text(json.dumps(value))
        path=target/'modelpack.json';value=json.loads(path.read_text());value['pack_id']=new
        value['profile_sha256']=sha256(profile);value['execution_contract']='raw_tensor_fp32_v1'
        model=value['models'][0];model['execution_contract']='raw_tensor_fp32_v1'
        model['input_contract'].update(width=512,height=288)
        model['output_contract']['max_output_bytes']={'out0':3024*65*4,'out1':3024*51*4}
        for key in ('use_winograd_convolution','use_sgemm_convolution'):del model['backend_options'][key]
        path.write_text(json.dumps(value))
        index.update(size=512,shape_id='rectangle512x288',input_width=512,input_height=288,
            convolution_kernel='default',execution_contract='raw_tensor_fp32_v1',
            eligibility_evidence_sha256='fe23f93f39a77ad8fc3f3b78a60dbb728e34749116fcd22478074b5d493dde74',
            fixture='android-yolo/resolution-feasibility/device-runs/seven-512/gpu-fp32-b39ee1b984a045e0a5d7c2b770191abc')
        helper.rehash(root,index);return target,index

    def test_exact_rectangle_uses_distinct_unity_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root);index,files=verify_runtime(root,512)
            self.assertEqual(unity_index(index,files,512)['version'],'yolo-local-fp32-rectangle512x288')

    def test_rehashed_square_wrong_bounds_or_source_rejected(self):
        for mutation in ('square','output','evidence','aspect','execution'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);pack,index=self.fixture(root);path=pack/'modelpack.json';value=json.loads(path.read_text())
                if mutation=='square':value['models'][0]['input_contract']['height']=512
                elif mutation=='output':value['models'][0]['output_contract']['max_output_bytes']['out0']=1
                elif mutation=='evidence':index['eligibility_evidence_sha256']='0'*64
                elif mutation=='aspect':index['source_aspect_ratio']='4:3'
                else:value['execution_contract']='raw_tensor_fp32_sgemm_v1'
                path.write_text(json.dumps(value));sgemm_fixture.SgemmStageTests().rehash(root,index)
                with self.assertRaises(ValueError):verify_runtime(root,512)

    def test_sgemm_not_qualified_for_smaller_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            with self.assertRaises(ValueError):verify_runtime(root,512,'sgemm')
