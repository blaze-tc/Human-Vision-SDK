import json, tempfile, unittest, shutil, subprocess, sys
from pathlib import Path
from tools.test.stage_android_yolo_eval import (verify_runtime, unity_index, sha256,
    verify_no_local_memory_identity, NO_LOCAL_MEMORY_SELECTED_FILES, NO_LOCAL_MEMORY_PROVENANCE)

class NoLocalMemoryStageTests(unittest.TestCase):
    def test_reviewed_identity_rejects_rehashed_replacement_models_and_sources(self):
        verify_no_local_memory_identity(dict(NO_LOCAL_MEMORY_PROVENANCE),dict(NO_LOCAL_MEMORY_SELECTED_FILES))
        for key in NO_LOCAL_MEMORY_SELECTED_FILES:
            files=dict(NO_LOCAL_MEMORY_SELECTED_FILES);files[key]='0'*64
            with self.assertRaises(ValueError):verify_no_local_memory_identity(dict(NO_LOCAL_MEMORY_PROVENANCE),files)
        for key in NO_LOCAL_MEMORY_PROVENANCE:
            index=dict(NO_LOCAL_MEMORY_PROVENANCE);index[key]='0'*64
            with self.assertRaises(ValueError):verify_no_local_memory_identity(index,dict(NO_LOCAL_MEMORY_SELECTED_FILES))

    def test_open_project_checks_same_kernel_before_and_after_copy(self):
        adapter=Path(__file__).resolve().parents[2]/'tools/test/stage_android_yolo_open_project.ps1'
        calls=[line for line in adapter.read_text(encoding='utf-8').splitlines()
               if line.startswith('& py -3 ') and 'stage_android_yolo_eval.py' in line]
        self.assertEqual(len(calls),2)
        for call in calls:
            self.assertIn('--convolution-kernel $kernel',call)

    def fixture(self, root):
        pack_id='yolov8n-pose-rectangle640x384-fp32-no-local-memory-local'
        pack=root/'modelpacks'/pack_id;pack.mkdir(parents=True)
        profile=root/'profiles/android-ncnn-vulkan.json';profile.parent.mkdir()
        profile.write_text(json.dumps(dict(local_evaluation_only=True,frame_policy='every_frame',
            body=dict(pipeline='pipeline.yolo.pose',modelPack=pack_id),hands=dict(enabled=False),
            backend=dict(preference=['backend.ncnn.vulkan'],allow_fallback=False))))
        (pack/'model.param').write_bytes(b'graph');(pack/'model.bin').write_bytes(b'weights')
        model=dict(role='body',format='ncnn',execution_contract='raw_tensor_fp32_no_local_memory_v1',decoder_id='yolov8_pose_dfl17_v1',
            param_path='model.param',param_sha256=sha256(pack/'model.param'),
            bin_path='model.bin',bin_sha256=sha256(pack/'model.bin'),
            backend_options=dict(use_packing_layout=True,use_subgroup_ops=False,use_fp16_packed=False,
                use_fp16_storage=False,use_fp16_arithmetic=False,use_winograd_convolution=True,use_sgemm_convolution=True,use_shader_local_memory=False),
            input_contract=dict(image_format='rgba8-unorm',color_order='rgb',crop='letterbox',resize_interpolation='bilinear',
                pad_rgb=[114,114,114],normalization=dict(mean=[0,0,0],norm=[1/255]*3),width=640,height=384,
                tensor_dtype='fp32',elempack=1,input_blob='in0'),
            output_contract=dict(decoder='yolov8_pose_dfl17_v1',output_blobs=['out0','out1'],
                max_output_bytes=dict(out0=5040*65*4,out1=5040*51*4)))
        manifest=dict(execution_contract='raw_tensor_fp32_no_local_memory_v1',pack_id=pack_id,pipeline_id='pipeline.yolo.pose',local_evaluation_only=True,max_people=8,
                      profile_sha256=sha256(profile),models=[model])
        (pack/'modelpack.json').write_text(json.dumps(manifest))
        index=dict(execution_contract='raw_tensor_fp32_no_local_memory_v1',local_evaluation_only=True,precision='fp32',size=640,convolution_kernel='no-local-memory',
            shape_id='rectangle640x384',input_width=640,input_height=384,source_aspect_ratio='16:9',
            fixture='android-yolo/device-runs/seven-640/gpu-fp32-no-local-memory-5c7bb015e67f4cef9129dddec5b5dcf9',
            eligibility_evidence_sha256='211a59e74cfe3d8d0d96b836f0bbe626d844e863e283acc955b2c3dfd7af5680')
        self.rehash(root,index);return pack,index

    def rehash(self,root,index):
        index['files']={p.relative_to(root).as_posix():sha256(p) for p in root.rglob('*') if p.is_file() and p.name!='index.json'}
        (root/'index.json').write_text(json.dumps(index))

    def test_explicit_no_local_memory_selection_and_unique_unity_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);_,index=self.fixture(root)
            parsed,files=verify_runtime(root,640,'no-local-memory')
            generated=unity_index(parsed,files,640,'no-local-memory')
            self.assertEqual(generated['version'],'yolo-local-fp32-rectangle640x384-no-local-memory')
            (root/'index.json').write_text(json.dumps(generated))
            verify_runtime(root,640,'no-local-memory')

    def test_no_local_memory_is_not_default_selection(self):
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
                with self.assertRaises(ValueError):verify_runtime(root,640,'no-local-memory')

    def test_rehashed_wrong_options_or_execution_cannot_change_kernel(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);pack,index=self.fixture(root)
            path=pack/'modelpack.json';original=json.loads(path.read_text())
            for key in ('option','execution'):
                value=json.loads(json.dumps(original))
                if key=='option':value['models'][0]['backend_options']['use_shader_local_memory']=True
                else:value['models'][0]['execution_contract']='raw_tensor_fp32_v1'
                path.write_text(json.dumps(value));self.rehash(root,index)
                with self.assertRaises(ValueError):verify_runtime(root,640,'no-local-memory')

    def test_rehashed_wrong_gate_identity_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);_,index=self.fixture(root)
            index['eligibility_evidence_sha256']='0'*64;self.rehash(root,index)
            with self.assertRaises(ValueError):verify_runtime(root,640,'no-local-memory')

    def test_rehashed_integer_cannot_impersonate_boolean_option(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);pack,index=self.fixture(root)
            path=pack/'modelpack.json';value=json.loads(path.read_text())
            value['models'][0]['backend_options']['use_shader_local_memory']=0
            path.write_text(json.dumps(value));self.rehash(root,index)
            with self.assertRaises(ValueError):verify_runtime(root,640,'no-local-memory')

    def test_unreviewed_kernel_or_size_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            for size,kernel in ((416,'no-local-memory'),(512,'no-local-memory'),(640,'unknown')):
                with self.assertRaises(ValueError):verify_runtime(root,size,kernel)

    def test_cli_rehashed_extra_provenance_is_rejected(self):
        root=Path(__file__).resolve().parents[2]
        runtime=root/'out/android-yolo/runtime-rectangle640x384-no-local-memory-verified'
        with tempfile.TemporaryDirectory() as temp:
            copied=Path(temp)/'runtime';shutil.copytree(runtime,copied)
            index=json.loads((copied/'index.json').read_text())
            index['eligibility_archives'][0]='unreviewed/archive'
            (copied/'index.json').write_text(json.dumps(index))
            result=subprocess.run([sys.executable,str(root/'tools/test/stage_android_yolo_eval.py'),
                '--verify-only','--runtime',str(copied),'--size','640','--convolution-kernel','no-local-memory'],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn('runtime index hash differs',result.stderr)

    def test_ps_wrapper_forwards_explicit_kernel_and_keeps_default(self):
        root=Path(__file__).resolve().parents[2]
        source=(root/'tools/test/stage_android_yolo_eval.ps1').read_text()
        self.assertIn("[ValidateSet('default','sgemm','no-local-memory')][string]$Kernel = 'default'",source)
        self.assertIn('--convolution-kernel $Kernel',source)
        self.assertIn("'rectangle640x384-no-local-memory'",source)

    def test_full_stage_serializes_exact_frozen_unity_index_with_lf(self):
        from tools.test.stage_android_yolo_eval import stage, NO_LOCAL_MEMORY_UNITY_INDEX_SHA, NO_LOCAL_MEMORY_NATIVE_SHA
        root=Path(__file__).resolve().parents[2]
        frozen=root/'out/android-yolo/no-local-memory-integration-frozen'
        runtime=frozen/'runtime';native=frozen/'libhumanvision.so'
        video=Path('E:/Project/Human Vision SDK/video-1.mp4')
        with tempfile.TemporaryDirectory(dir=root/'out/android-yolo',prefix='no-local-memory-full-stage-test-') as temporary:
            project=stage(runtime,native,video,Path(temporary)/'evaluation',640,'no-local-memory')
            embedded=project/'Assets/StreamingAssets/HumanVision/Runtime'
            raw=(embedded/'index.json').read_bytes()
            self.assertNotIn(b'\r\n',raw)
            self.assertEqual(sha256(embedded/'index.json'),NO_LOCAL_MEMORY_UNITY_INDEX_SHA)
            source=json.loads((runtime/'index.json').read_text())
            self.assertEqual(json.loads(raw),unity_index(source,source['files'],640,'no-local-memory'))
            self.assertEqual(sha256(native),NO_LOCAL_MEMORY_NATIVE_SHA)
            for relative,digest in source['files'].items():
                self.assertEqual(sha256(embedded/relative),digest)
                self.assertEqual(sha256(project/relative),digest)
            self.assertEqual(sha256(project/'Assets/StreamingAssets/HumanVision/Diagnostic'/video.name),sha256(video))
