// ARM64 pixel conversion evidence only: this executable runs no RKNN model.
// Compile with the Android NDK, -O3 -std=c++17 -Iruntime, then run via adb.
#include "plugins/backend/rknn/rknn_native_input.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace humanvision::runtime::rknn;
int main() {
    NativeInputTransform transform;
    const float means[][3]={{0,0,0},{127.5f,128,127},{0,0,0},{0,0,0}};
    const float norms[][3]={{1.f/255,1.f/255,1.f/255},{1.f/128,1.f/128,1.f/128},
        {0x1p-25f,-0x1p-25f,0x1p-24f},{1,2,-1}};
    unsigned checked=0;
    for(unsigned t=0;t<4;++t) {
        if(!transform.Configure(means[t],norms[t]))return 1;
        for(unsigned c=0;c<3;++c)for(unsigned v=0;v<256;++v) {
            const float value=(float(v)-means[t][c])*norms[t][c];
            const __fp16 reference=static_cast<__fp16>(value);
            uint16_t bits;std::memcpy(&bits,&reference,2);
            if(transform.Value(c,uint8_t(v))!=bits){std::fprintf(stderr,"FP16 mismatch %u/%u/%u\n",t,c,v);return 2;}
            ++checked;
        }
    }
    if(!transform.Configure(means[0],norms[0]))return 3;
    std::vector<uint8_t> rgb(512*288*3);
    for(size_t i=0;i<rgb.size();++i)rgb[i]=uint8_t(i*37);
    std::printf("{\"rk3588_performance_verified\":false,\"model_executed\":false,\"fp16_values_checked\":%u,\"cases\":[",checked);
    bool first=true;
    for(unsigned channels:{3u,8u})for(unsigned stride:{512u,528u}) {
        NativeInputLayout layout;const unsigned bytes=stride*288*channels*2;
        if(!layout.Configure(512,288,stride,channels,bytes))return 4;
        std::vector<uint16_t> output(bytes/2);std::vector<double> times;
        volatile uint16_t observed=0;
        for(int i=0;i<530;++i) {
            const auto begin=std::chrono::steady_clock::now();
            if(!transform.Pack(rgb.data(),uint32_t(rgb.size()),layout,output.data(),bytes))return 5;
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            observed=output[size_t(i*997)%output.size()];
            if(i>=30)times.push_back(ms);
        }
        // Validate every pixel and padding after the timed runs.
        for(unsigned y=0;y<288;++y)for(unsigned x=0;x<stride;++x)for(unsigned c=0;c<channels;++c) {
            const uint16_t expected=x<512&&c<3?transform.Value(c,rgb[(y*512+x)*3+c]):0;
            if(output[(y*stride+x)*channels+c]!=expected)return 6;
        }
        std::sort(times.begin(),times.end());
        std::printf("%s{\"channels\":%u,\"stride\":%u,\"p50_ms\":%.6f,\"p95_ms\":%.6f}",first?"":",",channels,stride,times[250],times[475]);
        first=false;(void)observed;
    }
    std::puts("]}");return 0;
}
