"""Byte-closure tests for local staging; fixture bytes never represent inference."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import stage_main_cpu_yolo as stage_tool


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payload(value):
    return (json.dumps(value, indent=2) + '\n').encode()


PROFILE = {
    'schema_version': 1, 'profile': 'android-cpu-nohands',
    'body': {'pipeline': 'pipeline.yolo.cpu', 'modelPack': 'yolov8n-pose-512x288-ort'},
    'hands': {'enabled': False, 'fps': 15}, 'body_fps': 30,
    'output': {'hz': 60},
    'backend': {'preference': ['backend.ort.cpu'], 'allow_fallback': False},
}


def model_manifest(model_hash):
    return {
        'schema_version': 1, 'pack_id': 'yolov8n-pose-512x288-ort',
        'pack_version': '1.0.0', 'pipeline_id': 'pipeline.yolo.cpu',
        'max_people': 8, 'capabilities': ['body_pose', 'multi_person'],
        'local_evaluation_only': True, 'distribution_qualified': False,
        'hardware_fps_acceptance': False,
        'models': [{
            'role': 'body', 'format': 'onnx', 'asset_path': 'body.onnx',
            'sha256': model_hash, 'decoder_id': 'yolov8_pose_dfl17_v1',
            'source': 'Packaging fixture only', 'license': 'No model in this fixture',
            'input_contract': {
                'width': 512, 'height': 288, 'tensor_layout': 'NCHW',
                'tensor_dtype': 'fp32', 'color_order': 'RGB', 'input_blob': 'in0',
                'crop_mode': 'letterbox', 'pad_rgb': [114, 114, 114],
                'normalization': {'mean': [0, 0, 0], 'norm': [1/255]*3},
            },
            'output_contract': {
                'decoder': 'yolov8_pose_dfl17_v1', 'output_blobs': ['out0', 'out1'],
                'tensor_dtype': 'fp32', 'rows': 3024, 'columns': [65, 51],
                'max_output_bytes': {'out0': 786240, 'out1': 616896},
            },
        }],
    }


class StageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.baseline = self.root/'baseline'
        self.source = self.root/'source'
        self.output = self.root/'new-stage'
        self.model = self.root/'recovered.onnx'
        self.model.write_bytes(b'packaging-only-onnx-fixture')
        self.native = self.root/'libhumanvision.so'
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        elf[16:20] = b'\x03\x00\xb7\x00'
        self.native.write_bytes(elf + b'packaging-only-native-fixture')
        self.native_sha = digest(self.native.read_bytes())
        self.model_sha = digest(self.model.read_bytes())
        self.files = {
            'profiles/android-cpu-nohands.json': payload({'old_cpu_policy': True}),
            'profiles/android-ncnn-vulkan-quality-low.json': b'original-gpu-profile',
            'profiles/android-rknn-npu-quality-low.json': b'original-npu-profile',
            'modelpacks/original-gpu/model.bin': b'original-gpu-weights',
            'modelpacks/original-npu/body.rknn': b'original-npu-weights',
        }
        for number in range(42):
            self.files[f'original/{number:02}.bin'] = f'preserve-{number}'.encode()
        for name, data in self.files.items():
            target = self.baseline/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.source_receipt = self.root/'source-receipt.json'
        self.index = {
            'binding_correction': {
                'composition_id': 'original-composition',
                'sdk_native_sha256': stage_tool.BASELINE_SDK_SHA256,
                'input_native_sha256': stage_tool.INPUT_SHA256,
                'windows_native_sha256': stage_tool.WINDOWS_SHA256,
                'distribution_qualified': False, 'hardware_fps_acceptance': False,
            },
            'npu_experimental': {'runtime_sha256': stage_tool.VENDOR_SHA256},
            'quality_delivery': {'original_evidence': 'retained'},
            'local_evaluation_only': True, 'hardware_fps_acceptance': False,
            'files': [{'path': name, 'sha256': digest(data)} for name, data in self.files.items()],
        }
        self.index_patch = patch.object(stage_tool, 'BASELINE_INDEX_SHA256', '')
        self.index_patch.start()
        self.addCleanup(self.index_patch.stop)
        self.receipt_patch = patch.object(stage_tool, 'SOURCE_RECEIPT_SHA256', '')
        self.receipt_patch.start()
        self.addCleanup(self.receipt_patch.stop)
        self.model_patch = patch.object(stage_tool, 'MODEL_SHA256', self.model_sha)
        self.model_patch.start()
        self.addCleanup(self.model_patch.stop)
        self.seal_baseline()
        self.write_templates()

    def seal_baseline(self):
        index_data = payload(self.index)
        (self.baseline/'index.json').write_bytes(index_data)
        seal = payload({'schema_version': 1, 'stage_id': 'local-qualified-runtime',
                        'local_evaluation_only': True, 'index_sha256': digest(index_data)})
        (self.baseline/'staged-runtime.json').write_bytes(seal)
        restore = {'main_commit': 'ba1815abe8fa07a134fda8c308a8d1312b5a05fd',
                   'sdk': '0.4.0-preview.7', 'input': '0.1.0-preview.5', 'runtime_files': 47,
                   'files': {'runtime/'+name: digest(data) for name, data in self.files.items()}}
        restore['files'].update({'runtime/index.json': digest(index_data),
                                 'runtime/staged-runtime.json': digest(seal)})
        restore_data = payload(restore)
        self.source_receipt.write_bytes(restore_data)
        stage_tool.BASELINE_INDEX_SHA256 = digest(index_data)
        stage_tool.SOURCE_RECEIPT_SHA256 = digest(restore_data)

    def write_templates(self, profile=None, manifest=None):
        self.pack = self.source/'modelpacks/yolov8n-pose-512x288-ort'
        self.pack.mkdir(parents=True, exist_ok=True)
        (self.source/'profiles').mkdir(exist_ok=True)
        (self.source/'profiles/android-cpu-nohands.json').write_bytes(payload(profile or PROFILE))
        (self.pack/'manifest.json').write_bytes(payload(manifest or model_manifest(self.model_sha)))
        component = {'id': 'modelpack.yolov8n-pose-512x288-ort', 'type': 'modelpack',
                     'version': '1.0.0', 'api_version': 1, 'capabilities': ['body_pose', 'multi_person'],
                     'dependencies': ['pipeline.yolo.cpu'], 'test_targets': ['humanvision_native_tests']}
        (self.pack/'component.json').write_bytes(payload(component))
        for name in ('README.md', 'SOURCES.md'):
            (self.pack/name).write_bytes(b'Packaging fixture metadata only.\n')

    def run_stage(self, **kwargs):
        return stage_tool.stage(self.baseline, self.source_receipt, self.model, self.native,
                                self.native_sha, self.output, source_root=self.source, **kwargs)

    def test_stage_changes_only_cpu_profile_and_adds_complete_pack(self):
        result = self.run_stage()
        runtime = self.output/'Runtime'
        index = json.loads((runtime/'index.json').read_bytes())
        self.assertEqual(len(index['files']), 52)
        for name, data in self.files.items():
            if name != 'profiles/android-cpu-nohands.json':
                self.assertEqual((runtime/name).read_bytes(), data, name)
        self.assertEqual(json.loads((runtime/'profiles/android-cpu-nohands.json').read_bytes()), PROFILE)
        self.assertEqual((runtime/'modelpacks/yolov8n-pose-512x288-ort/body.onnx').read_bytes(), self.model.read_bytes())
        self.assertEqual(index['npu_experimental'], self.index['npu_experimental'])
        self.assertEqual(index['quality_delivery'], self.index['quality_delivery'])
        self.assertEqual(index['binding_correction']['sdk_native_sha256'], self.native_sha)
        self.assertEqual(index['binding_correction']['input_native_sha256'], stage_tool.INPUT_SHA256)
        self.assertEqual(index['binding_correction']['windows_native_sha256'], stage_tool.WINDOWS_SHA256)
        self.assertFalse(result['hardware_fps_acceptance'])
        self.assertFalse(result['distribution_qualified'])
        self.assertEqual(len(result['preserved_files']), 46)
        self.assertEqual(list(result['changed_files']), ['profiles/android-cpu-nohands.json'])
        self.assertEqual(len(result['added_files']), 5)
        self.assertEqual((self.output/'native/libhumanvision.so').read_bytes(), self.native.read_bytes())
        self.assertEqual((self.baseline/'profiles/android-cpu-nohands.json').read_bytes(), self.files['profiles/android-cpu-nohands.json'])
        self.assertEqual(stage_tool.read_stage(self.output)['files'][
            'modelpacks/yolov8n-pose-512x288-ort/body.onnx'], self.model.read_bytes())

    def test_wrong_seal_rejected_before_output(self):
        (self.baseline/'staged-runtime.json').write_bytes(payload({'index_sha256': '0'*64}))
        with self.assertRaisesRegex(ValueError, 'receipt|seal'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_changed_baseline_asset_rejected_before_output(self):
        (self.baseline/'modelpacks/original-gpu/model.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_missing_baseline_asset_rejected_before_output(self):
        (self.baseline/'modelpacks/original-npu/body.rknn').unlink()
        with self.assertRaisesRegex(ValueError, 'missing|Missing'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_traversal_in_index_rejected_before_output(self):
        self.index['files'][0]['path'] = '../outside.json'
        self.seal_baseline()
        with self.assertRaisesRegex(ValueError, 'path'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_wrong_original_source_receipt_rejected(self):
        self.source_receipt.write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, 'source receipt'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_unknown_model_rejected_before_output(self):
        profile = copy.deepcopy(PROFILE)
        profile['body']['modelPack'] = 'unknown-model'
        self.write_templates(profile=profile)
        with self.assertRaisesRegex(ValueError, 'profile|ModelPack'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_backend_fallback_rejected_before_output(self):
        profile = copy.deepcopy(PROFILE)
        profile['backend'] = {'preference': ['backend.ort.cpu', 'backend.ort.nnapi'], 'allow_fallback': True}
        self.write_templates(profile=profile)
        with self.assertRaisesRegex(ValueError, 'backend|profile'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_wrong_model_bytes_rejected_before_output(self):
        self.model.write_bytes(b'wrong-model')
        with self.assertRaisesRegex(ValueError, 'model.*hash|model.*SHA'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_model_asset_traversal_rejected_before_output(self):
        manifest = model_manifest(self.model_sha)
        manifest['models'][0]['asset_path'] = '../body.onnx'
        self.write_templates(manifest=manifest)
        with self.assertRaisesRegex(ValueError, 'asset|contract'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_missing_template_rejected_before_output(self):
        (self.pack/'SOURCES.md').unlink()
        with self.assertRaisesRegex(ValueError, 'missing|Missing'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_backend_override_in_model_rejected_before_output(self):
        manifest = model_manifest(self.model_sha)
        manifest['models'][0]['backend_options'] = {'provider': 'NNAPI'}
        self.write_templates(manifest=manifest)
        with self.assertRaisesRegex(ValueError, 'backend'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_native_hash_mismatch_rejected_before_output(self):
        self.native.write_bytes(self.native.read_bytes()+b'changed')
        with self.assertRaisesRegex(ValueError, 'native.*hash|native.*SHA'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_non_arm64_native_rejected_before_output(self):
        self.native.write_bytes(b'not-an-arm64-elf')
        self.native_sha = digest(self.native.read_bytes())
        with self.assertRaisesRegex(ValueError, 'ARM64|ELF'): self.run_stage()
        self.assertFalse(self.output.exists())

    def test_existing_output_never_overwritten(self):
        self.output.mkdir()
        (self.output/'keep.txt').write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'new output|fresh'): self.run_stage()
        self.assertEqual((self.output/'keep.txt').read_bytes(), b'preserve')

    def test_read_stage_rejects_resealed_unreviewed_gpu_change(self):
        self.run_stage()
        runtime = self.output/'Runtime'
        target = runtime/'modelpacks/original-gpu/model.bin'
        target.write_bytes(b'unreviewed-gpu-change')
        index = json.loads((runtime/'index.json').read_bytes())
        for row in index['files']:
            if row['path'] == 'modelpacks/original-gpu/model.bin': row['sha256'] = digest(target.read_bytes())
        index_data = payload(index)
        (runtime/'index.json').write_bytes(index_data)
        seal = json.loads((runtime/'staged-runtime.json').read_bytes())
        seal['index_sha256'] = digest(index_data)
        (runtime/'staged-runtime.json').write_bytes(payload(seal))
        with self.assertRaisesRegex(ValueError, 'stage.*hash|stage.*receipt|preserved'): stage_tool.read_stage(self.output)


if __name__ == '__main__': unittest.main()
