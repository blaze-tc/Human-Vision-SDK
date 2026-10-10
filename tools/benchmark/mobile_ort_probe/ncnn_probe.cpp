// Real FP32 model experiment. Includes output download, excludes image decode.
// Fixtures and outputs are retained so speed cannot replace numerical acceptance.
#include "net.h"
#include "gpu.h"
#include "command.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
int main(int argc,char** argv) {
 if(argc!=10) return 2;
 const int w=std::atoi(argv[4]),h=std::atoi(argv[5]),threads=std::atoi(argv[7]),runs=std::atoi(argv[8]);
 const std::string mode=argv[6];const bool gpu=mode!="cpu";
 if(w<32||h<32||w>960||h>640||w%32||h%32||threads<1||threads>8||runs<1||runs>200||
    (mode!="cpu"&&mode!="baseline"&&mode!="sgemm"&&mode!="no-local-memory"&&mode!="winograd23"))return 2;
 // Declare lifetime before Net/VkMat so all tensors/network die before reclaim.
 struct GpuLifetime {
  bool enabled;const ncnn::VulkanDevice* device=nullptr;
  ncnn::VkAllocator* blob=nullptr; ncnn::VkAllocator* staging=nullptr;
  explicit GpuLifetime(bool use):enabled(use) {
   if(!enabled)return;ncnn::create_gpu_instance();
   if(ncnn::get_gpu_count()<1)return;device=ncnn::get_gpu_device();
   blob=device->acquire_blob_allocator();staging=device->acquire_staging_allocator();
  }
  ~GpuLifetime(){if(device){if(blob)device->reclaim_blob_allocator(blob);if(staging)device->reclaim_staging_allocator(staging);}if(enabled)ncnn::destroy_gpu_instance();}
 } lifetime(gpu);
 auto* device=lifetime.device;auto* blob=lifetime.blob;auto* staging=lifetime.staging;
 if(gpu&&(!device||!blob||!staging))return 3;
 ncnn::Net net;
 net.opt.use_vulkan_compute=gpu;net.opt.num_threads=threads;
 net.opt.use_fp16_packed=false;net.opt.use_fp16_storage=false;net.opt.use_fp16_arithmetic=false;
 net.opt.use_subgroup_ops=false;net.opt.use_packing_layout=true;
 net.opt.use_winograd_convolution=mode!="sgemm";
 // Isolate the official 2x2-output-tile Winograd path. This keeps the FP32
 // tensor/model contract; it is a device experiment, not a shipping default.
 net.opt.use_winograd43_convolution=mode!="winograd23";
 net.opt.use_sgemm_convolution=true;net.opt.use_shader_local_memory=mode!="no-local-memory";
 net.opt.blob_vkallocator=blob;net.opt.workspace_vkallocator=blob;net.opt.staging_vkallocator=staging;
 if(net.load_param(argv[1])||net.load_model(argv[2]))return 4;
 ncnn::Mat input(w,h,3);FILE* f=std::fopen(argv[3],"rb");if(!f)return 5;
 for(int c=0;c<3;++c)if(std::fread(input.channel(c),sizeof(float),size_t(w)*h,f)!=size_t(w)*h){std::fclose(f);return 5;}
 if(std::fgetc(f)!=EOF){std::fclose(f);return 5;}std::fclose(f);
 for(int c=0;c<3;++c)for(size_t i=0;i<size_t(w)*h;++i)if(!std::isfinite(input.channel(c)[i]))return 5;
 ncnn::VkMat uploaded;
 if(gpu){ncnn::VkMat transferred;ncnn::VkCompute cmd(device);cmd.record_upload(input,transferred,net.opt);
  device->convert_packing(transferred,uploaded,1,1,cmd,net.opt);if(cmd.submit_and_wait()||uploaded.empty())return 6;}
 std::vector<double> times;ncnn::Mat outputs[2];const char* names[]{"out0","out1"};
 for(int i=0;i<runs+10;++i){
  const auto start=std::chrono::steady_clock::now();auto ex=net.create_extractor();
  if(gpu){ex.set_blob_vkallocator(blob);ex.set_workspace_vkallocator(blob);ex.set_staging_vkallocator(staging);
   if(ex.input("in0",uploaded))return 7;ncnn::VkCompute cmd(device);ncnn::VkMat raw[2],packed[2];
   for(int j=0;j<2;++j){if(ex.extract(names[j],raw[j],cmd))return 7;device->convert_packing(raw[j],packed[j],1,1,cmd,net.opt);
    auto download=net.opt;download.use_packing_layout=false;cmd.record_download(packed[j],outputs[j],download);}
   if(cmd.submit_and_wait())return 7;
  }else{if(ex.input("in0",input))return 7;for(int j=0;j<2;++j)if(ex.extract(names[j],outputs[j]))return 7;}
  if(i>=10)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
 for(int j=0;j<2;++j){auto& v=outputs[j];const int columns=j?51:65;const int rows=w*h/64+w*h/256+w*h/1024;
  if(v.empty()||v.dims!=2||v.w!=columns||v.h!=rows||v.elempack!=1||v.elembits()!=32)return 8;
  for(size_t k=0;k<size_t(columns)*rows;++k)if(!std::isfinite(static_cast<const float*>(v)[k]))return 8;
  f=std::fopen((std::string(argv[9])+"-"+names[j]+".fp32").c_str(),"wb");
  if(!f||std::fwrite(v,sizeof(float),size_t(columns)*rows,f)!=size_t(columns)*rows)return 9;std::fclose(f);
 }
 double sum=0;for(double ms:times)sum+=ms;std::sort(times.begin(),times.end());
 std::printf("{\"mode\":\"%s\",\"threads\":%d,\"winograd23\":%s,\"winograd43\":%s,\"mean_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f}\n",mode.c_str(),threads,net.opt.use_winograd23_convolution?"true":"false",net.opt.use_winograd43_convolution?"true":"false",sum/times.size(),times[(times.size()-1)*95/100],times.back());
 return 0;
}
