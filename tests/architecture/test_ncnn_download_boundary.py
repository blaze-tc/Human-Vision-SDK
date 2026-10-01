"""Compile both production output-download sites against pinned ncnn packing policy.

This exercises the actual options passed to record_download and the pinned
command.cpp destination-packing selection. It does not emulate GPU inference
or establish hardware parity; those require the integrated device run.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cl"), "Requires the pinned VS developer environment")
class DownloadBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "runtime/plugins/backend/ncnn/ncnn_android_session.cpp").read_text()
        pinned = ROOT / "out/ncnn-20260526/source/src/command.cpp"
        if not pinned.exists():
            raise unittest.SkipTest("Requires the pinned ncnn source")
        command = pinned.read_text()
        begin = command.index("void VkCompute::record_download(const VkMat& src, Mat& dst,")
        begin = command.index("    // resolve dst_elempack", begin)
        end = command.index("    // gpu cast to fp32", begin)
        packing_policy = command[begin:end]
        sites = re.findall(
            r"(?:        ncnn::Option download_option = option_;\n"
            r"(?:        download_option\.\w+ = false;\n)+)?"
            r"        slot.compute->record_download\(slot.fp32_outputs\[i\], slot.cpu_outputs\[i\], [^;]+;",
            source,
        )
        if len(sites) != 2:
            raise AssertionError(f"Expected both production output sites, found {len(sites)}")
        cls.folder = tempfile.TemporaryDirectory(prefix="ncnn-download-")
        folder = Path(cls.folder.name)
        harness = r'''
#include <array>
#include <cstdlib>
namespace ncnn {
struct Option {
    bool use_packing_layout=true, use_fp16_storage=true, use_fp16_packed=true;
    bool use_fp16_arithmetic=true;
    void* blob_vkallocator=reinterpret_cast<void*>(123);
};
struct Mat { int dims=2,w=65,h=2100,c=1,elempack=1; size_t elemsize=4; };
}
struct Command {
    void record_download(const ncnn::Mat& src, ncnn::Mat& dst, const ncnn::Option& opt) {
        // Actual pinned ncnn destination packing policy, not a rewritten formula.
        PINNED_POLICY
        dst=src; dst.elempack=dst_elempack; dst.elemsize=4u*dst_elempack;
        if(src.dims==2) dst.h=src.h*src.elempack/dst_elempack;
        if(src.dims==3) dst.c=src.c*src.elempack/dst_elempack;
        // FP16 flags must be disabled only for this CPU boundary; allocator and
        // unrelated execution options must survive the copy.
        if(opt.use_fp16_storage || opt.use_fp16_packed || !opt.use_fp16_arithmetic ||
           opt.blob_vkallocator!=reinterpret_cast<void*>(123)) std::exit(2);
    }
};
struct Slot { Command* compute; std::array<ncnn::Mat,1> fp32_outputs,cpu_outputs; };
int main(int argc,char** argv) {
    ncnn::Option option_; Command command; Slot slot{&command}; size_t i=0;
    const int mode=std::atoi(argv[1]);
    if(mode==1) { option_.use_fp16_storage=false; option_.use_fp16_packed=false; }
    if(mode==2) { slot.fp32_outputs[0].dims=3; slot.fp32_outputs[0].c=4; }
    PRODUCTION_SITE
    const auto& src=slot.fp32_outputs[0]; const auto& dst=slot.cpu_outputs[0];
    if(dst.elempack!=1 || dst.elemsize!=sizeof(float) || dst.h!=src.h || dst.c!=src.c) return 1;
    if(!option_.use_packing_layout || option_.use_fp16_storage!=(mode!=1) ||
       option_.use_fp16_packed!=(mode!=1)) return 3;
    return 0;
}
'''
        cls.executables = []
        for index, site in enumerate(sites):
            code = harness.replace("PINNED_POLICY", packing_policy).replace("PRODUCTION_SITE", site)
            cpp = folder / f"site{index}.cpp"
            cpp.write_text(code)
            exe = folder / f"site{index}.exe"
            built = subprocess.run(["cl", "/nologo", "/std:c++17", "/EHsc", str(cpp), f"/Fe:{exe}"],
                                   cwd=folder, capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stdout + built.stderr)
            cls.executables.append(exe)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def check(self, site, mode):
        result = subprocess.run([str(self.executables[site]), str(mode)])
        self.assertEqual(result.returncode, 0, "CPU output must remain FP32 pack1 with unchanged shape/options")

    def test_run_image_yolo_2100_rows_with_internal_packing(self):
        self.check(1, 1)

    def test_prepared_detector_yolo_2100_rows_with_internal_packing(self):
        self.check(0, 1)

    def test_run_image_fp16_network_download_is_fp32_pack1(self):
        self.check(1, 0)

    def test_prepared_detector_fp16_network_download_is_fp32_pack1(self):
        self.check(0, 0)

    def test_run_image_channel_packing_does_not_change_cpu_shape(self):
        self.check(1, 2)

    def test_prepared_detector_channel_packing_does_not_change_cpu_shape(self):
        self.check(0, 2)


if __name__ == "__main__":
    unittest.main()
