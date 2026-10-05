import json
import shutil
import tempfile
import unittest
from pathlib import Path
from tools.models.ncnn import yolo_pose_gate as gate
from tools.models.ncnn import yolo_rectangle960_device_gate as device_gate

from tools.models.ncnn import yolo_rectangle960_prepare as trial


class Rectangle960FixtureTests(unittest.TestCase):
    def archived_seven(self):
        index=json.loads(Path('tools/models/ncnn/yolo_rectangle960_gate_evidence.json').read_text())
        row=next(row for row in index['fixtures'] if row['fixture']=='seven-960')
        return Path(index['archive_base'])/row['archive']

    def test_real_960_recipe_and_all_seven_raised_left_arms(self):
        directory=self.archived_seven()
        report=device_gate.compare_directory(directory)
        self.assertTrue(report['passed'],report.get('error'))
        self.assertEqual(report['raised_left_arms'],{'cpu':7,'gpu-fp32':7})
        self.assertFalse(gate.compare_directory(directory,'gpu-fp32')['passed'])

    def test_changed_new_recipe_runner_log_and_output_hashes_rejected(self):
        for key in ('runner_source_sha256','runner_cmake_sha256','runner_sha256'):
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'run';shutil.copytree(self.archived_seven(),directory)
                path=directory/'execution.json';record=json.loads(path.read_text());record[key]='0'*64
                path.write_text(json.dumps(record));self.assertFalse(device_gate.compare_directory(directory)['passed'])
        for name in ('android-gpu-fp32.log','ncnn-gpu-fp32-out0.fp32'):
            with self.subTest(file=name),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'run';shutil.copytree(self.archived_seven(),directory)
                path=directory/name;path.write_bytes(path.read_bytes()+b'x')
                self.assertFalse(device_gate.compare_directory(directory)['passed'])

    def test_derived_canvas_pixels_and_placement_cannot_be_rehashed(self):
        import cv2
        for mutation in ('placement','pixels'):
            with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'one';trial.prepare_fixture('one-960',Path('out/android-yolo'),directory)
                path=directory/'fixture.json';record=json.loads(path.read_text())
                if mutation=='placement':record['derived_canvas']['left']+=1
                else:
                    image=cv2.imread(str(directory/'source.png'));image[0,0,0]=113
                    cv2.imwrite(str(directory/'source.png'),image);record['source_png_sha256']=trial.sha(directory/'source.png')
                path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):trial.validate_fixture(directory)

    def test_explicit_recipe_preserves_default_and_rejects_wrong_hashes(self):
        evidence=json.loads(Path('tools/models/ncnn/yolo_model_gate_evidence.json').read_text())
        item=next(row for row in evidence['fixtures'] if row['fixture']=='seven-640')
        directory=Path('out')/item['archive']
        self.assertTrue(gate.compare_directory(directory,'gpu-fp32')['passed'])
        recipe=gate.runner_recipe('gpu-fp32')
        self.assertTrue(gate.compare_directory(directory,'gpu-fp32',runner_recipe_override=recipe)['passed'])
        with tempfile.TemporaryDirectory() as temp:
            wrong=Path(temp)/'wrong.cpp';wrong.write_text('wrong source')
            result=gate.compare_directory(directory,'gpu-fp32',runner_recipe_override=(wrong,recipe[1]))
            self.assertFalse(result['passed']);self.assertIn('runner source hash',result['error'])
            result=gate.compare_directory(directory,'gpu-fp32',runner_recipe_override=(recipe[0],wrong))
            self.assertFalse(result['passed']);self.assertIn('runner build recipe hash',result['error'])

    def test_frozen_960_index_revalidates_every_archived_artifact(self):
        path=Path('tools/models/ncnn/yolo_rectangle960_gate_evidence.json')
        self.assertEqual(trial.sha(path),'b1d634d79c6793bb0970e49b7b3975e901b9560a3f38f9f581e57e554de82683')
        index=json.loads(path.read_text());source,recipe=device_gate.runner_recipe('gpu-fp32')
        self.assertEqual(trial.sha(source),index['runner_source_sha256'])
        self.assertEqual(trial.sha(recipe),index['runner_cmake_sha256'])
        self.assertEqual(index['model_hashes'],gate.MODEL_HASHES)
        self.assertFalse(index['hardware_fps_acceptance']);self.assertFalse(index['production_gpu_ahb_input_acceptance'])
        for row in index['fixtures']:
            directory=Path(index['archive_base'])/row['archive']
            for name,digest in row['artifact_sha256'].items():self.assertEqual(trial.sha(directory/name),digest)
            report=device_gate.compare_directory(directory)
            self.assertTrue(report['passed'],report.get('error'))
            self.assertEqual(report,json.loads((directory/'comparison.json').read_text()))

    def test_exact_source_pixels_padding_and_dynamic_shapes(self):
        with tempfile.TemporaryDirectory() as temp:
            for name, shape in [('seven-960', (960,576)), ('one-960',(960,576)), ('empty-960',(960,576))]:
                directory=Path(temp)/name
                record=trial.prepare_fixture(name, Path('out/android-yolo'), directory)
                trial.validate_fixture(directory)
                g=record['geometry']
                self.assertEqual((g['width'],g['height']),shape)
                self.assertEqual(record['raw_shapes'],{'out0':[11340,65],'out1':[11340,51]})
                if name=='seven-960':
                    self.assertEqual((g['resized_height'],g['top']), (540,18))
                    self.assertEqual(record['sequential_frame_index'],1500)
                    self.assertEqual(len(record['annotations']),7)

    def test_reject_wrong_geometry_even_rehashed_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)/'empty-960'; trial.prepare_fixture('empty-960',Path('out/android-yolo'),directory)
            path=directory/'fixture.json'; record=json.loads(path.read_text()); record['geometry']['height']=320
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'geometry'): trial.validate_fixture(directory)

    def test_reject_tampered_input_and_source_even_rehashed(self):
        for file,key in [('input.fp32','input_sha256'),('source.png','source_png_sha256')]:
            with self.subTest(file=file),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'empty-960'; trial.prepare_fixture('empty-960',Path('out/android-yolo'),directory)
                path=directory/file; content=bytearray(path.read_bytes()); content[-1]^=1; path.write_bytes(content)
                metadata=directory/'fixture.json'; record=json.loads(metadata.read_text()); record[key]=trial.sha(path); metadata.write_text(json.dumps(record))
                with self.assertRaises(ValueError):trial.validate_fixture(directory)

    def test_reject_unknown_fixture_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError,'unknown'):trial.prepare_fixture('other-960',Path('out/android-yolo'),Path(temp)/'new')
            directory=Path(temp)/'seven-640';directory.mkdir(); sentinel=directory/'keep';sentinel.write_text('historical')
            with self.assertRaisesRegex(ValueError,'exists'):trial.prepare_fixture('empty-960',Path('out/android-yolo'),directory)
            self.assertEqual(sentinel.read_text(),'historical')

    def test_pinned_original_source_rejects_rehashed_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp); shutil.copytree(Path('out/android-yolo/one-640'),base/'one-640')
            path=base/'one-640/source.png'; path.write_bytes(path.read_bytes()+b'tamper')
            record_path=base/'one-640/fixture.json';record=json.loads(record_path.read_text());record['source_png_sha256']=trial.sha(path);record_path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'pinned'):trial.prepare_fixture('one-960',base,base/'new')

    def test_unknown_metadata_and_changed_limits_are_rejected(self):
        for key,value in [('fixture','unknown'),('limits',{}),('raw_shapes',{'out0':[3780,65],'out1':[3780,51]}),('hardware_fps_acceptance',True)]:
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'empty-960';trial.prepare_fixture('empty-960',Path('out/android-yolo'),directory)
                path=directory/'fixture.json';record=json.loads(path.read_text());record[key]=value;path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):trial.validate_fixture(directory)

    def test_annotation_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)/'seven-960';trial.prepare_fixture('seven-960',Path('out/android-yolo'),directory)
            path=directory/'fixture.json';record=json.loads(path.read_text());record['annotations'][0]['bbox_xyxy'][0]=0;path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'provenance'):trial.validate_fixture(directory)


if __name__=='__main__':unittest.main()
