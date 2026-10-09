"""Private APK composition gate; never a device qualification assertion."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.package import stage_rknn_experimental as stage

ROOT = Path(__file__).resolve().parents[2]

class ExperimentalStageTests(unittest.TestCase):
    def test_exact_immutable_model_and_receipts_only(self):
        receipt = dict(target='rk3588', toolkit_version='2.3.2',
            source_onnx_sha256=stage.ONNX_SHA, rknn_sha256=stage.MODEL_SHA,
            precision_request='non-quantized', deployment_ready=False,
            device_performance_verified=False)
        simulator = dict(offline_numerical_passed=True, deployment_ready=False,
            device_performance_verified=False,
            identity=dict(rknn_sha256=stage.MODEL_SHA,
                          validation_index_sha256=stage.BANK_SHA),
            fixtures=[dict(passed=True,expected_people=n) for n in [7,1,0]])
        stage.validate_receipts(receipt, simulator)
        mutations = [(receipt, 'precision_request', 'int8'),
                     (receipt, 'rknn_sha256', '0'*64),
                     (receipt, 'device_performance_verified', True),
                     (simulator, 'offline_numerical_passed', False)]
        for source,key,value in mutations:
            broken=copy.deepcopy(source);broken[key]=value
            with self.assertRaises(ValueError):
                stage.validate_receipts(broken if source is receipt else receipt,
                                        broken if source is simulator else simulator)
        broken=copy.deepcopy(simulator);broken['fixtures'][0]['passed']=False
        with self.assertRaises(ValueError): stage.validate_receipts(receipt,broken)

    def test_profile_and_manifest_bind_to_strict_private_tensor_route(self):
        profile=json.loads((stage.TEMPLATES/'android-rknn-npu-quality-low.json').read_text())
        pack=stage.build_manifest(profile, '244a28f1bc9e0b1665fe27a3b35e774a732f9f62f752b5f6faed376e28bef4bb', 'fe29f669bd5fafecba7a21797cad35f99b9f21b540c419a5f479d98cd03687f9')
        self.assertEqual(profile['backend'],dict(preference=['backend.rknn'],allow_fallback=False))
        self.assertFalse(profile['hands']['enabled'])
        self.assertEqual(pack['profile_sha256'],stage.json_hash(profile))
        self.assertFalse(pack['qualification']['device_performance_verified'])
        self.assertTrue(pack['experimental'])
        self.assertTrue(pack['local_evaluation_only'])
        self.assertEqual(pack['models'][0]['input_contract']['tensor_dtype'],'uint8')
        self.assertEqual(pack['models'][0]['sha256'],stage.MODEL_SHA)
        self.assertEqual(pack['pipeline_id'],'pipeline.yolo.tensor')

    def test_manifest_builder_refuses_receipts_that_native_pipeline_would_reject(self):
        profile=json.loads((stage.TEMPLATES/'android-rknn-npu-quality-low.json').read_text())
        with self.assertRaisesRegex(ValueError, 'receipt SHA'):
            stage.build_manifest(profile, 'a'*64, 'b'*64)

    def test_modified_receipt_bytes_are_rejected_before_any_pack_is_written(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);conversion=root/'conversion.json';comparison=root/'comparison.json'
            conversion.write_text('{"precision_request":"non-quantized"}',encoding='utf-8')
            comparison.write_text('{"offline_numerical_passed":true}',encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'receipt SHA'):
                stage.validate_receipt_files(conversion, comparison)
            self.assertFalse((root/'modelpacks').exists())

    def test_reindex_preserves_vulkan_and_records_truthful_experimental_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'profiles').mkdir()
            (root/'profiles'/'vulkan.json').write_text('original')
            (root/'index.json').write_text(json.dumps(dict(version='private',
                distribution_qualified=False, binding_correction=dict(sdk_native_sha256='old'),files=[])))
            stage.reindex(root,'native','input','runtime')
            index=json.loads((root/'index.json').read_text())
            self.assertEqual(index['binding_correction']['sdk_native_sha256'],'native')
            self.assertFalse(index['npu_experimental']['device_performance_verified'])
            self.assertEqual(index['files'],[dict(path='profiles/vulkan.json',
                sha256=hashlib.sha256(b'original').hexdigest())])
            self.assertEqual((root/'profiles'/'vulkan.json').read_text(),'original')
            receipt=json.loads((root/'staged-runtime.json').read_text())
            self.assertEqual(receipt['stage_id'],'local-qualified-runtime')
            self.assertTrue(receipt['local_evaluation_only'])
            self.assertEqual(receipt['index_sha256'],stage.sha(root/'index.json'))
            # Re-running must not index the preservation receipt into itself.
            stage.reindex(root,'native','input','runtime')
            self.assertEqual(json.loads((root/'index.json').read_text())['files'],index['files'])
            self.assertEqual(json.loads((root/'staged-runtime.json').read_text())['index_sha256'],stage.sha(root/'index.json'))

if __name__ == '__main__': unittest.main()
