import json,tempfile,unittest
from pathlib import Path
from tests.reference import test_yolo_sgemm_stage as sgemm_fixture
from tools.test.stage_android_yolo_eval import (verify_runtime,unity_index,sha256,
    verify_rectangle576_identity,RECTANGLE576_SELECTED_FILES,RECTANGLE576_PROVENANCE)

class Rectangle576StageTests(unittest.TestCase):
    def test_reviewed_model_and_source_identity_reject_rehashed_tampering(self):
        verify_rectangle576_identity(dict(RECTANGLE576_PROVENANCE),dict(RECTANGLE576_SELECTED_FILES))
        for key in RECTANGLE576_SELECTED_FILES:
            files=dict(RECTANGLE576_SELECTED_FILES);files[key]='0'*64
            with self.assertRaises(ValueError):verify_rectangle576_identity(dict(RECTANGLE576_PROVENANCE),files)
        for key in RECTANGLE576_PROVENANCE:
            index=dict(RECTANGLE576_PROVENANCE);index[key]='0'*64
            with self.assertRaises(ValueError):verify_rectangle576_identity(index,dict(RECTANGLE576_SELECTED_FILES))

    def test_real_frozen_runtime_and_unity_index_reject_rehashed_tampering(self):
        import shutil
        from tools.test.stage_android_yolo_eval import ROOT, verify_reviewed_rectangle576_runtime
        source=ROOT/'out/android-yolo/runtime-rectangle576x352-arm-verified'
        for mutation in ('none','index','selected-file','unity-version'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temp:
                root=Path(temp)/'runtime';shutil.copytree(source,root)
                index,files=verify_runtime(root,576)
                if mutation=='none':
                    verify_reviewed_rectangle576_runtime(root,index,files)
                    generated=unity_index(index,files,576)
                    (root/'index.json').write_text(json.dumps(generated,indent=2)+'\n',encoding='utf-8',newline='\n')
                    index,files=verify_runtime(root,576)
                    verify_reviewed_rectangle576_runtime(root,index,files)
                    continue
                if mutation=='index':index['runner_sha256']='0'*64
                elif mutation=='selected-file':files['profiles/android-ncnn-vulkan.json']='0'*64
                else:index['version']='unqualified'
                (root/'index.json').write_text(json.dumps(index))
                with self.assertRaises(ValueError):verify_reviewed_rectangle576_runtime(root,index,files)

    def test_legacy_validator_index_uses_verified_real_assets(self):
        import shutil
        from tools.test import stage_android_yolo_eval as stage
        source=stage.ROOT/'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'legacy';shutil.copytree(source,root)
            lines=stage.legacy_sha256_index(root).splitlines()
            self.assertEqual(len(lines),20)
            for line in lines:
                digest,relative=line.split('  ',1)
                self.assertEqual(stage.sha256(root/relative),digest)
            model=json.loads((root/'modelpack.json').read_text())['models'][0]
            (root/model['bin_path']).write_bytes(b'tampered')
            with self.assertRaises(ValueError):stage.legacy_sha256_index(root)

    def fixture(self,root):
        helper=sgemm_fixture.SgemmStageTests();pack,index=helper.fixture(root)
        new='yolov8n-pose-rectangle576x352-fp32-local'
        target=pack.with_name(new);pack.rename(target)
        profile=root/'profiles/android-ncnn-vulkan.json'
        value=json.loads(profile.read_text());value['body']['modelPack']=new;profile.write_text(json.dumps(value))
        path=target/'modelpack.json';value=json.loads(path.read_text());value['pack_id']=new
        value['profile_sha256']=sha256(profile);value['execution_contract']='raw_tensor_fp32_v1'
        model=value['models'][0];model['execution_contract']='raw_tensor_fp32_v1'
        model['input_contract'].update(width=576,height=352)
        model['output_contract']['max_output_bytes']={'out0':4158*65*4,'out1':4158*51*4}
        for key in ('use_winograd_convolution','use_sgemm_convolution'):del model['backend_options'][key]
        path.write_text(json.dumps(value))
        index.update(size=576,shape_id='rectangle576x352',input_width=576,input_height=352,
            convolution_kernel='default',execution_contract='raw_tensor_fp32_v1',
            eligibility_evidence_sha256='c3389a91ff01e4bfe2a293b52b67b0fc21608ba9cb5916ab60786259acd52cee',
            fixture='android-yolo/rectangle576-eligibility-20261001/device-runs/seven-576/gpu-fp32-f190aa94c8e540bd9fff84857da7e824')
        helper.rehash(root,index);return target,index

    def test_exact_rectangle_uses_distinct_unity_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root);index,files=verify_runtime(root,576)
            self.assertEqual(unity_index(index,files,576)['version'],'yolo-local-fp32-rectangle576x352')

    def test_rehashed_square_wrong_bounds_or_source_rejected(self):
        for mutation in ('square','portrait','output','evidence','aspect','execution','production','fp16'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);pack,index=self.fixture(root);path=pack/'modelpack.json';value=json.loads(path.read_text())
                if mutation=='square':value['models'][0]['input_contract']['height']=576
                elif mutation=='portrait':value['models'][0]['input_contract'].update(width=384,height=576)
                elif mutation=='production':value['local_evaluation_only']=False
                elif mutation=='fp16':value['models'][0]['backend_options']['use_fp16_storage']=True
                elif mutation=='output':value['models'][0]['output_contract']['max_output_bytes']['out0']=1
                elif mutation=='evidence':index['eligibility_evidence_sha256']='0'*64
                elif mutation=='aspect':index['source_aspect_ratio']='4:3'
                else:value['execution_contract']='raw_tensor_fp32_sgemm_v1'
                path.write_text(json.dumps(value));sgemm_fixture.SgemmStageTests().rehash(root,index)
                with self.assertRaises(ValueError):verify_runtime(root,576)

    def test_sgemm_not_qualified_for_smaller_shape(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            with self.assertRaises(ValueError):verify_runtime(root,576,'sgemm')
