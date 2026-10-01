import json
import shutil
import tempfile
import unittest
from pathlib import Path

from tools.models.ncnn import yolo_rectangle576_prepare as trial


class Rectangle576FixtureTests(unittest.TestCase):
    def test_exact_source_pixels_padding_and_dynamic_shapes(self):
        with tempfile.TemporaryDirectory() as temp:
            for name, shape in [('seven-576', (576,352)), ('one-576',(384,576)), ('empty-576',(576,352))]:
                directory=Path(temp)/name
                record=trial.prepare_fixture(name, Path('out/android-yolo'), directory)
                trial.validate_fixture(directory)
                g=record['geometry']
                self.assertEqual((g['width'],g['height']),shape)
                self.assertEqual(record['raw_shapes'],{'out0':[4158,65],'out1':[4158,51]} if name!='one-576' else {'out0':[4536,65],'out1':[4536,51]})
                if name=='seven-576':
                    self.assertEqual((g['resized_height'],g['top']), (324,14))
                    self.assertEqual(record['sequential_frame_index'],1500)
                    self.assertEqual(len(record['annotations']),7)

    def test_reject_wrong_geometry_even_rehashed_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)/'empty-576'; trial.prepare_fixture('empty-576',Path('out/android-yolo'),directory)
            path=directory/'fixture.json'; record=json.loads(path.read_text()); record['geometry']['height']=320
            path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'geometry'): trial.validate_fixture(directory)

    def test_reject_tampered_input_and_source_even_rehashed(self):
        for file,key in [('input.fp32','input_sha256'),('source.png','source_png_sha256')]:
            with self.subTest(file=file),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'empty-576'; trial.prepare_fixture('empty-576',Path('out/android-yolo'),directory)
                path=directory/file; content=bytearray(path.read_bytes()); content[-1]^=1; path.write_bytes(content)
                metadata=directory/'fixture.json'; record=json.loads(metadata.read_text()); record[key]=trial.sha(path); metadata.write_text(json.dumps(record))
                with self.assertRaises(ValueError):trial.validate_fixture(directory)

    def test_reject_unknown_fixture_and_existing_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError,'unknown'):trial.prepare_fixture('other-576',Path('out/android-yolo'),Path(temp)/'new')
            directory=Path(temp)/'seven-640';directory.mkdir(); sentinel=directory/'keep';sentinel.write_text('historical')
            with self.assertRaisesRegex(ValueError,'exists'):trial.prepare_fixture('empty-576',Path('out/android-yolo'),directory)
            self.assertEqual(sentinel.read_text(),'historical')

    def test_pinned_original_source_rejects_rehashed_pixels(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp); shutil.copytree(Path('out/android-yolo/one-640'),base/'one-640')
            path=base/'one-640/source.png'; path.write_bytes(path.read_bytes()+b'tamper')
            record_path=base/'one-640/fixture.json';record=json.loads(record_path.read_text());record['source_png_sha256']=trial.sha(path);record_path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'pinned'):trial.prepare_fixture('one-576',base,base/'new')

    def test_unknown_metadata_and_changed_limits_are_rejected(self):
        for key,value in [('fixture','unknown'),('limits',{}),('raw_shapes',{'out0':[3780,65],'out1':[3780,51]}),('hardware_fps_acceptance',True)]:
            with self.subTest(key=key),tempfile.TemporaryDirectory() as temp:
                directory=Path(temp)/'empty-576';trial.prepare_fixture('empty-576',Path('out/android-yolo'),directory)
                path=directory/'fixture.json';record=json.loads(path.read_text());record[key]=value;path.write_text(json.dumps(record))
                with self.assertRaises(ValueError):trial.validate_fixture(directory)

    def test_annotation_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)/'seven-576';trial.prepare_fixture('seven-576',Path('out/android-yolo'),directory)
            path=directory/'fixture.json';record=json.loads(path.read_text());record['annotations'][0]['bbox_xyxy'][0]=0;path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'provenance'):trial.validate_fixture(directory)


if __name__=='__main__':unittest.main()
