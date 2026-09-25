#include "net.h"
#include "gpu.h"
#include "command.h"
#include <cmath>
#include <cstdio>
#include <string>
int main(int argc,char**argv){
 if(argc!=5)return 2;
 ncnn::create_gpu_instance(); if(ncnn::get_gpu_count()<1)return 3;
 ncnn::Net net;net.opt.use_vulkan_compute=true;net.opt.use_fp16_packed=true;net.opt.use_fp16_storage=true;net.opt.use_fp16_arithmetic=false;net.opt.use_subgroup_ops=false;
 if(net.load_param(argv[1]))return 4;const unsigned char unused[4]={};if(net.load_model(unused))return 5;
 for(auto*l:net.layers())if(!l->support_vulkan)return 6;
 ncnn::Mat input(48,26);FILE*f=fopen(argv[2],"rb");if(!f)return 7;size_t n=fread(input.data,4,48*26,f);fclose(f);if(n!=48*26)return 8;
 auto*dev=net.vulkan_device();auto opt=net.opt;opt.blob_vkallocator=dev->acquire_blob_allocator();opt.workspace_vkallocator=opt.blob_vkallocator;opt.staging_vkallocator=dev->acquire_staging_allocator();
 ncnn::VkMat staging,gpu;ncnn::VkCompute upload(dev);upload.record_upload(input,staging,opt);dev->convert_packing(staging,gpu,1,2,upload,opt);if(upload.submit_and_wait())return 9;
 printf("Vulkan layers=%zu unsupported=0 fp16_packed=1 storage=1 arithmetic=0 subgroup=0 input_pack=%d bits=%d\n",net.layers().size(),gpu.elempack,gpu.elembits());
 if(gpu.elembits()!=16)return 10;auto ex=net.create_extractor();ex.set_light_mode(false);if(ex.input("in0",gpu))return 11;ncnn::Mat output;if(ex.extract("out0",output))return 12;
 printf("output dims=%d w=%d h=%d c=%d pack=%d bits=%d total=%zu\n",output.dims,output.w,output.h,output.c,output.elempack,output.elembits(),output.total()); if(output.w*output.h*output.c!=26||output.elempack!=1||output.elembits()!=32)return 13;
 f=fopen(argv[3],"wb");if(!f)return 14;fwrite(output.data,4,26,f);fclose(f);int failures=0,saturated=0;
 for(int r=0;r<26;r++){double sum=0;for(int k=0;k<48;k++){double v=ncnn::float16_to_float32(ncnn::float32_to_float16(input.row(r)[k]));sum+=v*v;}double expected=sqrt(sum),actual=((float*)output.data)[r];bool ok=std::isfinite(actual)&&fabs(actual-expected)<=expected*.001+1e-5;failures+=!ok;saturated+=sum>65504;printf("row=%d square_sum=%.12g expected=%.12g actual=%.12g pass=%d\n",r,sum,expected,actual,ok);}
 printf("failures=%d saturated_source_rows=%d\n",failures,saturated);
 if(std::string(argv[4])=="original"){ncnn::Mat sums;if(ex.extract("sum",sums))return 15;f=fopen((std::string(argv[3])+".sums").c_str(),"wb");if(!f)return 16;fwrite(sums.data,4,26,f);fclose(f);}
 return failures?20:0;
}

