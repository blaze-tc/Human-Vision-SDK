// C2 diagnostic: valid pre-uploaded FP16 pack1 input through graph, FP32 outputs,
// RTMDet decode and person NMS. Build against the pinned Android ncnn release.
#include "net.h"
#include "gpu.h"
#include "command.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
struct Box { float x1, y1, x2, y2, score; };
static float IoU(const Box& a, const Box& b) {
    const float w = std::max(0.f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
    const float h = std::max(0.f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
    const float aa = (a.x2-a.x1)*(a.y2-a.y1), bb = (b.x2-b.x1)*(b.y2-b.y1);
    return w*h/(aa+bb-w*h+1e-9f);
}
static int Decode(const ncnn::Mat& cls, const ncnn::Mat& bbox,
                  std::vector<Box>& candidates, std::vector<Box>& selected) {
    if (cls.w != 1 || cls.h != 2100 || bbox.w != 4 || bbox.h != 2100 ||
        cls.elempack != 1 || bbox.elempack != 1 ||
        cls.elemsize != 4 || bbox.elemsize != 4) return -1;
    candidates.clear(); selected.clear();
    for (int i=0; i<2100; ++i) {
        const float score = 1.f/(1.f+std::exp(-cls.row(i)[0]));
        if (score < .35f) continue;
        const int level = i<1600 ? 0 : i<2000 ? 1 : 2;
        const int local = i-(level==0 ? 0 : level==1 ? 1600 : 2000);
        const int grid = level==0 ? 40 : level==1 ? 20 : 10;
        const float stride = float(8<<level);
        const float cx = (float(local%grid)+.5f)*stride;
        const float cy = (float(local/grid)+.5f)*stride;
        const float* d = bbox.row(i);
        Box b{cx-d[0],cy-d[1],cx+d[2],cy+d[3],score};
        if (b.x2>b.x1 && b.y2>b.y1) candidates.push_back(b);
    }
    std::sort(candidates.begin(),candidates.end(),[](const Box& a,const Box& b){return a.score>b.score;});
    if (candidates.size()>1000) candidates.resize(1000);
    for (const Box& b : candidates) {
        bool keep=true;
        for (const Box& s : selected) if (IoU(b,s)>.6f) {keep=false;break;}
        if (keep) selected.push_back(b);
        if (selected.size()==100) break;
    }
    return int(selected.size());
}
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    ncnn::create_gpu_instance(); if (ncnn::get_gpu_count()<1) return 3;
    ncnn::Net net;
    net.opt.use_vulkan_compute=true; net.opt.use_fp16_storage=true;
    net.opt.use_fp16_arithmetic=true; net.opt.use_fp16_packed=true;
    if (net.load_param(argv[1]) || net.load_model(argv[2])) return 4;
    std::vector<float> data(3*320*320);
    FILE* f=std::fopen(argv[3],"rb"); if (!f) return 5;
    const bool read=std::fread(data.data(),sizeof(float),data.size(),f)==data.size();
    std::fclose(f); if (!read) return 6;
    ncnn::Mat input(320,320,3);
    for (int c=0;c<3;++c) for (int y=0;y<320;++y) for (int x=0;x<320;++x)
        input.channel(c).row(y)[x]=data[(c*320+y)*320+x];
    ncnn::VulkanDevice* dev=ncnn::get_gpu_device();
    ncnn::VkAllocator* blob=dev->acquire_blob_allocator();
    ncnn::VkAllocator* staging=dev->acquire_staging_allocator();
    ncnn::Option opt=net.opt; opt.blob_vkallocator=blob; opt.staging_vkallocator=staging;
    ncnn::VkMat gpu_input;
    { ncnn::VkCompute cmd(dev); cmd.record_upload(input,gpu_input,opt);
      if (cmd.submit_and_wait()) return 7; }
    if (gpu_input.c!=3 || gpu_input.elempack!=1 || gpu_input.elembits()!=16) return 8;
    std::vector<Box> candidates, selected; candidates.reserve(2100); selected.reserve(100);
    std::vector<double> totals; totals.reserve(100);
    std::printf("input,c=%d,pack=%d,bits=%d\n",gpu_input.c,gpu_input.elempack,gpu_input.elembits());
    std::printf("sample,graph_ms,download_ms,decode_nms_ms,detector_ms,count\n");
    for (int i=0;i<120;++i) {
        const auto t0=Clock::now(); ncnn::VkMat cls_gpu,bbox_gpu;
        { ncnn::VkCompute cmd(dev); ncnn::Extractor ex=net.create_extractor();
          ex.set_blob_vkallocator(blob); ex.set_workspace_vkallocator(blob);
          ex.set_staging_vkallocator(staging);
          if (ex.input("in0",gpu_input) || ex.extract("cls",cls_gpu,cmd) ||
              ex.extract("bbox",bbox_gpu,cmd) || cmd.submit_and_wait()) return 9; }
        const auto t1=Clock::now(); ncnn::Mat cls,bbox;
        { ncnn::VkCompute cmd(dev); ncnn::VkMat cls_out,bbox_out;
          dev->convert_packing(cls_gpu,cls_out,1,1,cmd,opt);
          dev->convert_packing(bbox_gpu,bbox_out,1,1,cmd,opt);
          ncnn::Option download=opt; download.use_packing_layout=false;
          cmd.record_download(cls_out,cls,download); cmd.record_download(bbox_out,bbox,download);
          if (cmd.submit_and_wait()) return 10; }
        const auto t2=Clock::now(); const int count=Decode(cls,bbox,candidates,selected);
        if (count<0) return 11;
        const auto t3=Clock::now();
        if (i>=20) { const double total=Ms(t0,t3); totals.push_back(total);
            std::printf("%d,%.4f,%.4f,%.4f,%.4f,%d\n",i-20,Ms(t0,t1),Ms(t1,t2),Ms(t2,t3),total,count); }
    }
    std::sort(totals.begin(),totals.end());
    std::printf("summary,n=100,p50=%.4f,p95=%.4f,max=%.4f\n",totals[49],totals[94],totals.back());
    dev->reclaim_blob_allocator(blob); dev->reclaim_staging_allocator(staging);
    return 0;
}
