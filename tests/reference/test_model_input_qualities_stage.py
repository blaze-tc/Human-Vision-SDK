"""Q2 real immutable model/profile staging and semantic rejection tests."""
import importlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT/'out/input/production-correction/user-packages-v15/com.blazetc.humanvision/RuntimeData'
LOW = ROOT/'out/android-yolo/runtime-rectangle512x288-arm-verified'


class ModelInputQualityStageTests(unittest.TestCase):
    def implementation(self):
        try:
            return importlib.import_module('tools.models.ncnn.stage_model_input_qualities')
        except ModuleNotFoundError:
            self.fail('Q2 quality runtime staging is not implemented')

    def test_real_stage_preserves_every_original_indexed_byte_and_binds_three_shapes(self):
        m = self.implementation()
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as temp:
            stage = m.stage(BASE, LOW, Path(temp)/'runtime')
            m.validate_runtime(stage)
            original = json.loads((BASE/'index.json').read_text())
            for row in original['files']:
                self.assertEqual((BASE/row['path']).read_bytes(), (stage/row['path']).read_bytes())
            catalog = json.loads((stage/'model-input-qualities.json').read_text())
            self.assertEqual([(q['id'],q['width'],q['height']) for q in catalog['families'][0]['qualities']],
                             [('low',512,288),('medium',640,384),('high',960,576)])
            with self.assertRaises(ValueError): m.stage(BASE, LOW, stage)

    def test_rehashed_semantic_tampering_cannot_be_admitted(self):
        m = self.implementation()
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as temp:
            valid = m.stage(BASE, LOW, Path(temp)/'valid')
            for mutation in ('geometry','backend','fallback','weights','options','capacity','capabilities','extra-capability','profile-capabilities','source','license','recipe','output-bounds','normalization','duplicate','missing','unsafe','schema','default','unexpected','unknown-field','profile-binding','hash'):
                with self.subTest(mutation=mutation):
                    stage = Path(temp)/mutation; shutil.copytree(valid, stage)
                    catalog_path = stage/'model-input-qualities.json'
                    catalog = json.loads(catalog_path.read_text()); family = catalog['families'][0]
                    profile_path = stage/'profiles/android-ncnn-vulkan-quality-high.json'
                    profile = json.loads(profile_path.read_text())
                    pack_path = stage/'modelpacks'/profile['body']['modelPack']/'modelpack.json'
                    pack = json.loads(pack_path.read_text()); model = pack['models'][0]
                    if mutation=='geometry': model['input_contract']['height']=544
                    elif mutation=='backend': profile['backend']['preference']=['backend.ort.cpu']
                    elif mutation=='fallback': profile['backend']['allow_fallback']=True
                    elif mutation=='weights': model['bin_sha256']='0'*64
                    elif mutation=='options': model['backend_options']['use_fp16_storage']=True
                    elif mutation=='capacity': pack['max_people']=4
                    elif mutation=='capabilities': pack['capabilities'].remove('android-hardware-buffer')
                    elif mutation=='extra-capability': pack['capabilities'].append('hand_pose')
                    elif mutation=='profile-capabilities': profile['required_capabilities']=[]
                    elif mutation=='source': model['source']=''
                    elif mutation=='license': model['license']=''
                    elif mutation=='recipe': model['conversion_recipe']=''
                    elif mutation=='output-bounds': model['output_contract']['max_output_bytes']['out0']=1
                    elif mutation=='normalization': model['input_contract']['normalization']['norm'][0]=1
                    elif mutation=='duplicate': family['qualities'][2]=family['qualities'][0]
                    elif mutation=='missing': family['qualities'].pop()
                    elif mutation=='unsafe': family['qualities'][0]['profile']='../escape'
                    elif mutation=='schema': catalog['schema_version']=2
                    elif mutation=='default': family['default_quality']='high'
                    elif mutation=='unexpected': family['qualities'][2]['id']='ultra'
                    elif mutation=='unknown-field': family['fallback']=True
                    elif mutation=='profile-binding': pack['profile_sha256']='0'*64
                    else: (stage/'profiles/windows-pc-cpu.json').write_text('tamper')
                    m.write(catalog_path,catalog); m.write(profile_path,profile)
                    if mutation!='profile-binding': pack['profile_sha256']=m.sha(profile_path)
                    m.write(pack_path,pack)
                    index = json.loads((stage/'index.json').read_text())
                    for row in index['files']:
                        if row['path'] in [catalog_path.relative_to(stage).as_posix(),profile_path.relative_to(stage).as_posix(),pack_path.relative_to(stage).as_posix()]: row['sha256']=m.sha(stage/row['path'])
                    m.write(stage/'index.json',index)
                    with self.assertRaises(ValueError): m.validate_runtime(stage)

    def test_json_types_do_not_coerce_boolean_schema_or_numeric_fallback(self):
        m = self.implementation()
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as temp:
            valid = m.stage(BASE,LOW,Path(temp)/'valid')
            for mutation in ('schema-bool','fallback-number','packing-bool'):
                with self.subTest(mutation=mutation):
                    stage = Path(temp)/mutation; shutil.copytree(valid,stage)
                    path = stage/'profiles/android-ncnn-vulkan-quality-high.json'; profile=json.loads(path.read_text())
                    packpath=stage/'modelpacks'/profile['body']['modelPack']/'modelpack.json'; pack=json.loads(packpath.read_text())
                    if mutation=='schema-bool': profile['schema_version']=True
                    elif mutation=='fallback-number': profile['backend']['allow_fallback']=0
                    else: pack['models'][0]['input_contract']['elempack']=True
                    m.write(path,profile); pack['profile_sha256']=m.sha(path); m.write(packpath,pack)
                    index=json.loads((stage/'index.json').read_text())
                    for row in index['files']:
                        if row['path'] in (path.relative_to(stage).as_posix(),packpath.relative_to(stage).as_posix()): row['sha256']=m.sha(stage/row['path'])
                    m.write(stage/'index.json',index)
                    with self.assertRaises(ValueError): m.validate_runtime(stage)

    def test_frozen_source_identity_rejects_rehashed_low_profile(self):
        m = self.implementation()
        with tempfile.TemporaryDirectory(dir=ROOT/'out/android-yolo') as temp:
            low = Path(temp)/'low'; shutil.copytree(LOW,low)
            profile = low/'profiles/android-ncnn-vulkan.json'; profile.write_text(profile.read_text()+' ')
            index = json.loads((low/'index.json').read_text()); index['files']['profiles/android-ncnn-vulkan.json']=m.sha(profile)
            m.write(low/'index.json',index)
            with self.assertRaises(ValueError): m.stage(BASE,low,Path(temp)/'runtime')

if __name__=='__main__': unittest.main()
