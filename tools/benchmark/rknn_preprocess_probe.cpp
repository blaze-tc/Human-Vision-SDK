// Standalone pixel/timing probe. This executes no model and reports no NPU FPS.
#include "plugins/pipeline/yolo/yolo_rgb_preprocess.h"
#include <array>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr int width=512, height=288;
struct Axis {int first,second,weight0,weight1;};
template<size_t count> void BuildAxis(int source,std::array<Axis,count>& axes,bool horizontal) {
    const double scale=double(source)/count;
    for(size_t i=0;i<count;++i) {
        float fraction=float((i+.5)*scale-.5);
        int first=int(std::floor(fraction));fraction-=first;
        if(horizontal&&first<0){first=0;fraction=0;}
        if(horizontal&&first>=source-1){first=source-1;fraction=0;}
        axes[i]={std::clamp(first,0,source-1),std::clamp(first+1,0,source-1),
                 int(std::nearbyint((1.f-fraction)*2048.f)),int(std::nearbyint(fraction*2048.f))};
    }
}
// Frozen pre-change cached-coefficient loop. Construction is outside timing,
// just as coefficients are cached by the actual pipeline after warm-up.
struct Original {
    std::array<Axis,width> horizontal;
    std::array<Axis,height> vertical;
    Original(int w,int h){BuildAxis(w,horizontal,true);BuildAxis(h,vertical,false);}
    void Run(const HV_VideoFrame& frame,int channels,bool rgb,uint8_t* pixels) const {
        const auto* source=static_cast<const uint8_t*>(frame.data);
        for(int y=0;y<height;++y) {
            const auto& ay=vertical[y];
            const auto* top=source+size_t(ay.first)*frame.stride_bytes;
            const auto* bottom=source+size_t(ay.second)*frame.stride_bytes;
            for(int x=0;x<width;++x) {
                const auto& ax=horizontal[x];
                for(int channel=0;channel<3;++channel) {
                    const int offset=rgb?channel:2-channel;
                    const int a=top[size_t(ax.first)*channels+offset]*ax.weight0+
                                top[size_t(ax.second)*channels+offset]*ax.weight1;
                    const int b=bottom[size_t(ax.first)*channels+offset]*ax.weight0+
                                bottom[size_t(ax.second)*channels+offset]*ax.weight1;
                    const int value=(((ay.weight0*(a>>4))>>16)+((ay.weight1*(b>>4))>>16)+2)>>2;
                    pixels[(size_t(y)*width+x)*3+channel]=uint8_t(std::clamp(value,0,255));
                }
            }
        }
    }
};
volatile uint64_t observed=0;
template<class F> double Measure(F call,std::vector<uint8_t>& pixels,int iterations) {
    for(int i=0;i<30;++i)call();
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<iterations;++i){call();observed+=pixels[size_t(i*997)%pixels.size()];}
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/iterations;
}
}
int main(int argc,char** argv) {
    const int iterations=argc==2?std::atoi(argv[1]):300;
    if(iterations<10||iterations>10000)return 2;
    bool comma=false;
    std::printf("{\"kind\":\"actual-preprocess-pixel-and-wall-time\",\"npu_performance_verified\":false,\"cases\":[");
    for(auto format:{HV_PIXEL_RGB24,HV_PIXEL_BGR24,HV_PIXEL_RGBA32,HV_PIXEL_BGRA32}) {
        const int channels=format==HV_PIXEL_RGB24||format==HV_PIXEL_BGR24?3:4;
        const bool rgb=format==HV_PIXEL_RGB24||format==HV_PIXEL_RGBA32;
        for(int w:{512,1024,1280}) {
            const int h=w*9/16,stride=w*channels+19;
            std::vector<uint8_t> source(size_t(stride)*h),old_pixels(width*height*3),new_pixels(old_pixels.size());
            for(size_t i=0;i<source.size();++i)source[i]=uint8_t((i*71+(i/31)*113)&255);
            HV_VideoFrame frame{sizeof(frame),w,h,stride,format,42,123,source.data(),int32_t(source.size())};
            Original baseline(w,h);
            const auto old_call=[&]{baseline.Run(frame,channels,rgb,old_pixels.data());};
            const auto new_call=[&]{humanvision::runtime::yolo::TryFastRgbPreprocess(frame,new_pixels.data());};
            old_call();new_call();if(old_pixels!=new_pixels)return 3;
            double old_ms[2],new_ms[2];
            for(int trial=0;trial<2;++trial) {
                old_ms[trial]=Measure(old_call,old_pixels,iterations);
                new_ms[trial]=Measure(new_call,new_pixels,iterations);
            }
            if(old_pixels!=new_pixels)return 4;
            std::printf("%s{\"width\":%d,\"height\":%d,\"format\":%d,\"stride\":%d,\"iterations\":%d,"
                        "\"byte_equal\":true,\"baseline_ms\":[%.6f,%.6f],\"candidate_ms\":[%.6f,%.6f]}",
                        comma?",":"",w,h,int(format),stride,iterations,old_ms[0],old_ms[1],new_ms[0],new_ms[1]);
            comma=true;
        }
    }
    std::printf("],\"observed\":%llu}\n",static_cast<unsigned long long>(observed));return 0;
}
