// NanoDet-Plus-m 320 C2 diagnostic: pre-uploaded FP16 input to person NMS.
#include "net.h"
#include "gpu.h"
#include "command.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
struct Box {float x1,y1,x2,y2,score;};
static float iou(const Box&a,const Box&b){float w=std::max(0.f,std::min(a.x2,b.x2)-std::max(a.x1,b.x1));float h=std::max(0.f,std::min(a.y2,b.y2)-std::max(a.y1,b.y1));float aa=(a.x2-a.x1)*(a.y2-a.y1),bb=(b.x2-b.x1)*(b.y2-b.y1);return w*h/(aa+bb-w*h+1e-9f);}
static int decode(const ncnn::Mat& scores,const ncnn::Mat& dfl,std::vector<Box>& candidates,std::vector<Box>& selected){
 if(scores.w!=1||dfl.w!=32||scores.h!=2125||dfl.h!=2125||scores.elempack!=1||dfl.elempack!=1||scores.elemsize!=4||dfl.elemsize!=4)return -1;
 candidates.clear();selected.clear();
 for(int i=0;i<2125;++i){const float score=scores.row(i)[0];if(score<.35f)continue;
  int level=i<1600?0:i<2000?1:i<2100?2:3;
  int local=i-(level==0?0:level==1?1600:level==2?2000:2100);int grid=40>>level;float stride=float(8<<level);
  float d[4];for(int side=0;side<4;++side){const float*q=dfl.row(i)+side*8;float m=*std::max_element(q,q+8),denom=0,weighted=0;for(int j=0;j<8;++j){float e=std::exp(q[j]-m);denom+=e;weighted+=j*e;}d[side]=weighted/denom*stride;}
  float cx=float(local%grid)*stride,cy=float(local/grid)*stride;
  Box b{std::max(0.f,cx-d[0]),std::max(0.f,cy-d[1]),std::min(320.f,cx+d[2]),std::min(320.f,cy+d[3]),score};
  if(b.x2>b.x1&&b.y2>b.y1)candidates.push_back(b);
 }
 std::sort(candidates.begin(),candidates.end(),[](const Box&a,const Box&b){return a.score>b.score;});
 for(const Box&b:candidates){bool keep=true;for(const Box&s:selected)if(iou(b,s)>=.5f){keep=false;break;}if(keep)selected.push_back(b);}return int(selected.size());
}
int main(int argc,char**argv){
 if(argc!=4)return 2;ncnn::create_gpu_instance();if(ncnn::get_gpu_count()<1)return 3;
 ncnn::Net net;net.opt.use_vulkan_compute=true;net.opt.use_fp16_storage=true;net.opt.use_fp16_arithmetic=true;net.opt.use_fp16_packed=true;
 if(net.load_param(argv[1])||net.load_model(argv[2]))return 4;
 std::vector<float> data(3*320*320);FILE*f=std::fopen(argv[3],"rb");if(!f)return 5;bool read=std::fread(data.data(),4,data.size(),f)==data.size();std::fclose(f);if(!read)return 6;
 ncnn::Mat input(320,320,3);for(int c=0;c<3;++c)for(int y=0;y<320;++y)for(int x=0;x<320;++x)input.channel(c).row(y)[x]=data[(c*320+y)*320+x];
 ncnn::VulkanDevice*dev=ncnn::get_gpu_device();ncnn::VkAllocator*blob=dev->acquire_blob_allocator();ncnn::VkAllocator*staging=dev->acquire_staging_allocator();ncnn::Option opt=net.opt;opt.blob_vkallocator=blob;opt.staging_vkallocator=staging;
 ncnn::VkMat vin;{ncnn::VkCompute cmd(dev);cmd.record_upload(input,vin,opt);if(cmd.submit_and_wait())return 7;}
 if(vin.c!=3||vin.elempack!=1||vin.elembits()!=16)return 8;
 std::vector<Box> candidates,selected;candidates.reserve(2125);selected.reserve(2125);std::vector<double> totals;totals.reserve(100);
 std::printf("input,c=%d,pack=%d,bits=%d\n",vin.c,vin.elempack,vin.elembits());
 std::printf("sample,graph_ms,download_ms,decode_nms_ms,detector_ms,count\n");
 for(int i=0;i<120;++i){auto t0=Clock::now();ncnn::VkMat gpu_scores,gpu_dfl;
  {ncnn::VkCompute cmd(dev);ncnn::Extractor ex=net.create_extractor();ex.set_blob_vkallocator(blob);ex.set_workspace_vkallocator(blob);ex.set_staging_vkallocator(staging);int a=ex.input("data",vin),b=a?0:ex.extract("person",gpu_scores,cmd),c=(a||b)?0:ex.extract("dfl",gpu_dfl,cmd),d=(a||b||c)?0:cmd.submit_and_wait();if(a||b||c||d){std::fprintf(stderr,"extract error input=%d person=%d dfl=%d submit=%d\n",a,b,c,d);return 9;}}
  auto t1=Clock::now();ncnn::Mat scores,dfl;{ncnn::VkCompute cmd(dev);ncnn::VkMat out_scores,out_dfl;dev->convert_packing(gpu_scores,out_scores,1,1,cmd,opt);dev->convert_packing(gpu_dfl,out_dfl,1,1,cmd,opt);ncnn::Option download=opt;download.use_packing_layout=false;cmd.record_download(out_scores,scores,download);cmd.record_download(out_dfl,dfl,download);if(cmd.submit_and_wait())return 10;}
  auto t2=Clock::now();int count=decode(scores,dfl,candidates,selected);if(count<0){std::fprintf(stderr,"output shapes %d/%d/%d and %d/%d/%d\n",scores.w,scores.h,scores.c,dfl.w,dfl.h,dfl.c);return 11;}auto t3=Clock::now();
  if(i>=20){double total=ms(t0,t3);totals.push_back(total);std::printf("%d,%.4f,%.4f,%.4f,%.4f,%d\n",i-20,ms(t0,t1),ms(t1,t2),ms(t2,t3),total,count);}
 }
 std::sort(totals.begin(),totals.end());std::printf("summary,n=100,p50=%.4f,p95=%.4f,max=%.4f\n",totals[49],totals[94],totals.back());
 dev->reclaim_blob_allocator(blob);dev->reclaim_staging_allocator(staging);return 0;
}
