// Offline eligibility only: CPU fixture upload is never a production input route.
#include "net.h"
#include "gpu.h"
#include "command.h"
#include "layer.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

// Outlives Net/Extractor/VkMat; every command waits before allocation reclamation.
struct GpuAllocators {
    ncnn::VulkanDevice* device = nullptr;
    ncnn::VkAllocator* blob = nullptr;
    ncnn::VkAllocator* staging = nullptr;
    explicit GpuAllocators(bool gpu) {
        if (!gpu) return;
        device = ncnn::get_gpu_device();
        blob = device->acquire_blob_allocator();
        staging = device->acquire_staging_allocator();
    }
    ~GpuAllocators() {
        if (blob) device->reclaim_blob_allocator(blob);
        if (staging) device->reclaim_staging_allocator(staging);
    }
    GpuAllocators(const GpuAllocators&) = delete;
    GpuAllocators& operator=(const GpuAllocators&) = delete;
};

int main(int argc, char** argv) {
    if (argc != 7) { std::fprintf(stderr,"usage: yolo_sgemm_golden_runner param bin fixture-directory cpu|gpu-fp32-sgemm width height\n"); return 2; }
    const std::string mode=argv[4];
    const bool gpu=mode=="gpu-fp32-sgemm";
    const bool storage16=false;
    if (!gpu && mode!="cpu") return 2;
    const int w=std::atoi(argv[5]), h=std::atoi(argv[6]);
    if (w<32 || h<32 || w>640 || h>640 || w%32 || h%32) return 2;
    if (gpu) ncnn::create_gpu_instance();
    if (gpu && ncnn::get_gpu_count()<1) return 3;
    GpuAllocators allocators(gpu);
    if (gpu && (!allocators.blob || !allocators.staging)) return 13;
    ncnn::Net net;
    net.opt.blob_vkallocator=allocators.blob;
    net.opt.workspace_vkallocator=allocators.blob;
    net.opt.staging_vkallocator=allocators.staging;
    net.opt.use_vulkan_compute=gpu;
    net.opt.use_fp16_packed=storage16;
    net.opt.use_fp16_storage=storage16;
    net.opt.use_fp16_arithmetic=false;
    net.opt.use_subgroup_ops=false;
    net.opt.use_packing_layout=true; // Internal network packing; boundary remains pack1.
    net.opt.num_threads=2;
    if (gpu) {
        net.opt.use_winograd_convolution=false;
        net.opt.use_sgemm_convolution=true;
    }
    const auto diagnose_options = [&](const char* phase) {
        if (!gpu) return true;
        const auto& o=net.opt;
        std::fprintf(stderr,"%s mode=%s vulkan=%d storage16=%d packed16=%d arithmetic16=%d subgroup=%d packing=%d threads=%d winograd=%d sgemm=%d\n",phase,mode.c_str(),o.use_vulkan_compute,o.use_fp16_storage,o.use_fp16_packed,o.use_fp16_arithmetic,o.use_subgroup_ops,o.use_packing_layout,o.num_threads,o.use_winograd_convolution,o.use_sgemm_convolution);
        return o.use_vulkan_compute && !o.use_fp16_storage && !o.use_fp16_packed && !o.use_fp16_arithmetic && !o.use_subgroup_ops && o.use_packing_layout && o.num_threads==2 && !o.use_winograd_convolution && o.use_sgemm_convolution;
    };
    if (!diagnose_options("configured")) return 15;
    if (net.load_param(argv[1]) || net.load_model(argv[2])) return 4;
    if (!diagnose_options("effective")) return 15;
    int unsupported=0;
    for (const auto* layer:net.layers()) {
        if (gpu && !layer->support_vulkan) {
            std::fprintf(stderr,"UNSUPPORTED_VULKAN %s %s\n",layer->type.c_str(),layer->name.c_str()); ++unsupported;
        }
    }
    std::fprintf(stderr,"layers=%zu unsupported=%d storage16=%d arithmetic16=%d\n",net.layers().size(),unsupported,net.opt.use_fp16_storage,net.opt.use_fp16_arithmetic);
    if (unsupported) return 21;
    ncnn::Mat input(w,h,3);
    FILE* f=std::fopen((std::string(argv[3])+"/input.fp32").c_str(),"rb");
    if (!f) return 5;
    for (int c=0;c<3;++c) {
        if (std::fread(input.channel(c),sizeof(float),size_t(w)*h,f)!=size_t(w)*h) { std::fclose(f); return 6; }
        const float* values=input.channel(c);
        for (int i=0;i<w*h;++i) if (!std::isfinite(values[i])) { std::fclose(f); return 6; }
    }
    const int trailing=std::fgetc(f); std::fclose(f);
    if (trailing!=EOF) return 6;
    auto ex=net.create_extractor();
    ncnn::VkMat uploaded;
    if (gpu) {
        ex.set_blob_vkallocator(allocators.blob);
        ex.set_workspace_vkallocator(allocators.blob);
        ex.set_staging_vkallocator(allocators.staging);
        ncnn::VkMat transferred;
        ncnn::VkCompute cmd(allocators.device);
        cmd.record_upload(input,transferred,net.opt);
        if (transferred.empty()) return 7;
        // RGB has3 channels: explicit pack1, with independently named precision mode.
        // Candidate retains FP32 pack1 RGB and output boundaries.
        allocators.device->convert_packing(transferred,uploaded,1,storage16?2:1,cmd,net.opt);
        if (cmd.submit_and_wait()) return 7;
        std::fprintf(stderr,"input in0 dims=%d w=%d h=%d c=%d pack=%d bits=%d\n",uploaded.dims,uploaded.w,uploaded.h,uploaded.c,uploaded.elempack,uploaded.elembits());
        if (uploaded.empty() || uploaded.dims!=3 || uploaded.w!=w || uploaded.h!=h || uploaded.c!=3 || uploaded.elempack!=1 || uploaded.elembits()!=(storage16?16:32)) return 14;
        if (ex.input("in0",uploaded)) return 8;
    } else if (ex.input("in0",input)) return 8;
    const int rows=w*h/64+w*h/256+w*h/1024;
    for (const auto& name:std::vector<std::string>{"out0","out1"}) {
        const int columns=name=="out0"?65:51;
        ncnn::Mat output;
        if (gpu) {
            ncnn::VkCompute cmd(allocators.device);
            ncnn::VkMat result, fp32;
            if (ex.extract(name.c_str(),result,cmd)) return 9;
            // Output conversion also explicit: no packed/FP16 bytes mistaken for floats.
            allocators.device->convert_packing(result,fp32,1,1,cmd,net.opt);
            // convert_packing returns void and can leave dst empty on OOM.
            // Drain extraction/conversion commands while all tensors remain alive.
            if (fp32.empty() || fp32.dims!=2 || fp32.w!=columns || fp32.h!=rows || fp32.c!=1 || fp32.elempack!=1 || fp32.elembits()!=32) {
                if (cmd.submit_and_wait()) return 10;
                return 11;
            }
            ncnn::Option download=net.opt;
            download.use_packing_layout=false; download.use_fp16_storage=false; download.use_fp16_packed=false; download.use_fp16_arithmetic=false;
            cmd.record_download(fp32,output,download);
            if (cmd.submit_and_wait()) return 10;
        } else if (ex.extract(name.c_str(),output)) return 9;
        std::fprintf(stderr,"output %s dims=%d w=%d h=%d c=%d pack=%d bits=%d\n",name.c_str(),output.dims,output.w,output.h,output.c,output.elempack,output.elembits());
        if (output.empty() || output.dims!=2 || output.w!=columns || output.h!=rows || output.elempack!=1 || output.elembits()!=32) return 11;
        const float* values=output;
        for (size_t i=0;i<size_t(columns)*rows;++i) if (!std::isfinite(values[i])) return 16;
        f=std::fopen((std::string(argv[3])+"/ncnn-"+argv[4]+"-"+name+".fp32").c_str(),"wb");
        if (!f) return 12;
        const size_t count=size_t(columns)*rows;
        const bool written=std::fwrite(output,sizeof(float),count,f)==count;
        const bool closed=std::fclose(f)==0;
        if (!written || !closed) return 12;
    }
    return 0;
}
