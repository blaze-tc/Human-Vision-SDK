"""Task2 local model eligibility: raw golden, provenance and pack confinement."""
import copy
import json
import tempfile
import unittest
from pathlib import Path

from tools.models.ncnn.build_local_eval_pack import build_pack, verify_pack, verify_pose_golden, parse_vkmat_audit, VKMAT_AUDIT

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'out/c3-local-runtime/strict-vkmat'


class PoseGoldenTest(unittest.TestCase):
    def test_mat_only_golden_cannot_promote_pack(self):
        old=ROOT/'out/c3-local-runtime/first-norm-reducel2'
        if not (old/'golden/index.json').is_file():self.skipTest('local Mat-only evidence not generated')
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError,'VkMat'):
                build_pack(ROOT/'out/c2-local-detector',old,Path(directory)/'pack')

    def test_vkmat_audit_rejects_missing_duplicate_and_wrong_contract(self):
        good='HV_POSE_AUDIT '+json.dumps(VKMAT_AUDIT)
        self.assertEqual(parse_vkmat_audit(good),VKMAT_AUDIT)
        for log in ('',good+'\n'+good):
            with self.assertRaises(ValueError):parse_vkmat_audit(log)
        for key,value in [('input_route','mat'),('input_pack',1),('input_bits',32),('fp16_storage',False),('fp16_arithmetic',True),('subgroup',True),('vulkan_layers',165),('unsupported_layers',1),('output_route','mat'),('x_bytes',39935),('fp16_storage',1)]:
            with self.subTest(key=key,value=value):
                bad=copy.deepcopy(VKMAT_AUDIT);bad[key]=value
                with self.assertRaises(ValueError):parse_vkmat_audit('HV_POSE_AUDIT '+json.dumps(bad))

    def test_pack_rejects_rehashed_vkmat_audit_tampering(self):
        from tools.models.ncnn.model_contract import canonical_json, sha256_file
        if not (EVIDENCE/'golden/index.json').is_file():self.skipTest('local device evidence missing')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'pack'
            manifest=build_pack(ROOT/'out/c2-local-detector',EVIDENCE,root)
            path=root/'provenance/pose-vkmat-route.json'
            original=json.loads(path.read_text())
            for label in ('audit','runner','model','coverage','output'):
                bad=copy.deepcopy(original)
                if label=='audit':bad['runs'][0]['audit']['input_route']='mat'
                elif label=='runner':bad['runner_sha256']='0'*64
                elif label=='model':bad['model_sha256']['param']='0'*64
                elif label=='coverage':bad['runs'].pop()
                else:bad['runs'][0]['x_sha256']='0'*64
                path.write_text(canonical_json(bad),encoding='utf-8')
                changed=copy.deepcopy(manifest)
                next(a for a in changed['evidence'] if a['path']=='provenance/pose-vkmat-route.json')['sha256']=sha256_file(path)
                with self.subTest(label=label):
                    with self.assertRaises(ValueError):verify_pack(root,changed)

    def test_original_mirrored_confidence_is_rejected(self):
        from tools.models.ncnn.compare_pose_outputs import compare_pose, fixture_image_size
        import json
        folder=ROOT/'out/c3-local-runtime/non-subgroup/golden/mirrored'
        if not folder.is_dir(): self.skipTest('local diagnostic evidence not downloaded')
        ref,cand,repeat=[json.loads((folder/(n+'.json')).read_text()) for n in ('reference','candidate','repeat')]
        with self.assertRaisesRegex(ValueError,'confidence'):
            compare_pose(ref,cand,ref['bbox'],fixture_image_size(folder/'image.png'),repeat)

    def test_four_real_cases_and_hashed_local_pack(self):
        if not (EVIDENCE/'golden/index.json').is_file(): self.skipTest('local device evidence not generated')
        verify_pose_golden(EVIDENCE/'golden')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)/'pack'
            manifest=build_pack(ROOT/'out/c2-local-detector',EVIDENCE,root)
            self.assertTrue(manifest['local_evaluation_only'])
            self.assertEqual(manifest['schema_version'],2)
            self.assertEqual(manifest['models'][1]['input_contract']['elempack'],4)
            self.assertEqual(manifest['models'][1]['output_contract']['max_output_bytes'],{'simcc_x':39936,'simcc_y':53248})
            self.assertEqual(manifest['models'][1]['backend_options'],{'use_subgroup_ops':False,'use_fp16_arithmetic':False})
            verify_pack(root)
            for key,value in [('checkpoint_sha256','0'*64),('param_sha256','0'*64),('bin_sha256','0'*64),('param_path','../../outside.param'),('backend_options',{'use_subgroup_ops':True,'use_fp16_arithmetic':False})]:
                with self.subTest(key=key):
                    bad=copy.deepcopy(manifest);bad['models'][1][key]=value
                    with self.assertRaises(ValueError):verify_pack(root,bad)
            bad=copy.deepcopy(manifest);del bad['models'][1]['backend_options']
            with self.assertRaises(ValueError):verify_pack(root,bad)
            (root/'body/model.bin').write_bytes(b'corrupt')
            with self.assertRaises(ValueError):verify_pack(root,manifest)


if __name__=='__main__':unittest.main()
