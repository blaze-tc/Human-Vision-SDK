import json
import tempfile
import unittest
from pathlib import Path

from tools.test.stage_android_yolo_eval import verify_runtime, require_new_output, sha256, unity_index


class StageTests(unittest.TestCase):
    def fixture(self, root):
        pack = root / 'modelpacks/yolo-local'
        pack.mkdir(parents=True)
        profile = root / 'profiles/android-ncnn-vulkan.json'
        profile.parent.mkdir()
        profile.write_text(json.dumps({'local_evaluation_only': True, 'frame_policy': 'every_frame',
            'body': {'pipeline': 'pipeline.yolo.pose', 'modelPack': 'yolo-local'},
            'backend': {'preference': ['backend.ncnn.vulkan'], 'allow_fallback': False},
            'hands': {'enabled': False}}))
        (pack / 'model.bin').write_bytes(b'weights')
        (pack / 'model.param').write_bytes(b'graph')
        manifest = pack / 'modelpack.json'
        manifest.write_text(json.dumps({'pack_id': 'yolo-local', 'pipeline_id': 'pipeline.yolo.pose',
            'local_evaluation_only': True, 'max_people': 8, 'profile_sha256': sha256(profile),
            'models': [{'param_path': 'model.param', 'param_sha256': sha256(pack/'model.param'),
                'bin_path': 'model.bin', 'bin_sha256': sha256(pack/'model.bin'),
                'input_contract': {'width': 320, 'height': 320, 'tensor_dtype': 'fp32'},
                'backend_options': {'use_packing_layout': True, 'use_subgroup_ops': False,
                    'use_fp16_packed': False, 'use_fp16_storage': False, 'use_fp16_arithmetic': False}}]}))
        files = {p.relative_to(root).as_posix(): sha256(p) for p in root.rglob('*') if p.is_file()}
        (root/'index.json').write_text(json.dumps({'size': 320, 'precision': 'fp32', 'local_evaluation_only': True, 'files': files}))
        return profile, pack

    def test_tampered_weights_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); _, pack=self.fixture(root)
            verify_runtime(root, 320)
            (pack/'model.bin').write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError, 'hash'): verify_runtime(root, 320)

    def test_rehashed_wrong_selection_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); profile, _=self.fixture(root)
            doc=json.loads(profile.read_text()); doc['body']['pipeline']='pipeline.topdown.body'
            profile.write_text(json.dumps(doc))
            index=json.loads((root/'index.json').read_text()); index['files']['profiles/android-ncnn-vulkan.json']=sha256(profile)
            (root/'index.json').write_text(json.dumps(index))
            with self.assertRaisesRegex(ValueError, 'selection'): verify_runtime(root, 320)

    def test_existing_output_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(FileExistsError): require_new_output(Path(temp))

    def test_generated_unity_index_has_required_version_and_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); self.fixture(root)
            index, files=verify_runtime(root,320)
            generated=unity_index(index,files,320)
            self.assertEqual(generated['version'],'yolo-local-fp32-square320')
            self.assertIsInstance(generated['files'],list)
            self.assertEqual({row['path']:row['sha256'] for row in generated['files']},files)
            (root/'index.json').write_text(json.dumps(generated))
            verify_runtime(root,320)
            del generated['version']
            (root/'index.json').write_text(json.dumps(generated))
            with self.assertRaisesRegex(ValueError,'Unity index version'): verify_runtime(root,320)


if __name__ == '__main__': unittest.main()
