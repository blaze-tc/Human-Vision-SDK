from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ImportWriteStateTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cl'), 'Run under the pinned VS developer environment for behavioral test')
    def test_actual_import_body_records_first_read_dependency_for_both_branches(self):
        source = (ROOT/'runtime/plugins/backend/ncnn/ncnn_android_session.cpp').read_text()
        body = 'bool AndroidSession::RecordRgbImport('+source.split('bool AndroidSession::RecordRgbImport(',1)[1].split('\nbool AndroidSession::Initialize',1)[0]
        harness = r'''
#include <cassert>
#include <memory>
#include <string>
constexpr int VK_IMAGE_LAYOUT_GENERAL=1,VK_QUEUE_FAMILY_EXTERNAL_KHR=2;
constexpr int VK_ACCESS_SHADER_WRITE_BIT=64,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT=2048;
struct State { int access_flags=0,stage_flags=1; };
struct Mat { State* data=nullptr;int w=5,h=3,c=4,elempack=1;
 bool empty()const{return data==nullptr;}int elembits()const{return 32;} };
int barriers=0;
void reader(Mat& m){ if(m.data->access_flags==64&&m.data->stage_flags==2048)++barriers;
 else assert(false&&"first reader missed shader-write dependency"); }
namespace ncnn {struct VkCompute {void record_import_android_hardware_buffer(int*,int,Mat& dst,int,int,int){
 assert(dst.data->access_flags==0&&dst.data->stage_flags==1); // Pinned importer leaves state untouched.
}};}
struct Device {struct Info{int compute_queue_family_index(){return 0;}}info;
 void convert_packing(Mat& source,Mat&,int,int,ncnn::VkCompute&,int){reader(source);} };
struct Slot {Mat imported_rgb,imported_rgba,rgba_planes;std::unique_ptr<int> import_pipeline;int image=0;};
struct AndroidSession {Device* device_;int option_=0;bool RecordRgbImport(Slot&,ncnn::VkCompute&,std::string&);};
'''
        harness += body+r'''
int main(){for(bool packed:{false,true}){State rgb,rgba,planes;Slot slot;slot.imported_rgb.data=&rgb;
 if(packed)slot.imported_rgba.data=&rgba;slot.rgba_planes.data=&planes;
 Device device;AndroidSession session{&device};ncnn::VkCompute command;std::string error;
 int before=barriers;assert(session.RecordRgbImport(slot,command,error));
 if(!packed)reader(slot.imported_rgb);assert(barriers==before+1);
}return 0;}
'''
        with tempfile.TemporaryDirectory(prefix='import-state-') as folder:
            path=Path(folder);(path/'test.cpp').write_text(harness)
            compile_result=subprocess.run(['cl','/nologo','/std:c++17','/EHsc','test.cpp','/Fe:test.exe'],cwd=path,capture_output=True,text=True)
            self.assertEqual(compile_result.returncode,0,compile_result.stdout+compile_result.stderr)
            run=subprocess.run([str(path/'test.exe')],cwd=path,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,'Actual production body missed first-use dependency on aligned or packed branch')

    def test_aligned_and_non_aligned_first_use_declare_compute_write(self):
        source = (ROOT/'runtime/plugins/backend/ncnn/ncnn_android_session.cpp').read_text()
        body = source.split('bool AndroidSession::RecordRgbImport(', 1)[1].split('\nbool AndroidSession::Initialize', 1)[0]
        record = body.index('cmd.record_import_android_hardware_buffer')
        aligned_return = body.index('if(slot.imported_rgba.empty()) return true;')
        unpack = body.index('device_->convert_packing')
        for assignment in ('target.data->access_flags=VK_ACCESS_SHADER_WRITE_BIT;',
                           'target.data->stage_flags=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;'):
            with self.subTest(assignment=assignment):
                self.assertLess(record, body.index(assignment))
                self.assertLess(body.index(assignment), aligned_return)
                self.assertLess(body.index(assignment), unpack)


if __name__ == '__main__':
    unittest.main()
