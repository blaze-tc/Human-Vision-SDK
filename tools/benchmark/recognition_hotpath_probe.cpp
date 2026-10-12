// ARM64 CPU hot-path oracle/benchmark; it does NOT run an NPU model.
// The caller supplies pre-change headers/decoder under -I<snapshot>/baseline.
// Baseline and candidate run alternately in one process on identical buffers.
#include "plugins/backend/rknn/rknn_native_input.h"
#include "plugins/backend/rknn/rknn_native_output.h"
#include "plugins/pipeline/yolo/yolo_decoder.h"
#include "common/finite_tensor.h"
#define humanvision baseline_humanvision
#include "baseline/rknn_native_input.h"
#include "baseline/rknn_native_output.h"
#include "baseline/yolo_decoder.h"
#undef humanvision
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {
volatile unsigned observed=0;
template<class A,class B> void Time(const char* label,A baseline,B candidate) {
    std::vector<double> a,b;
    auto call=[](auto fn){const auto t=std::chrono::steady_clock::now();
        if(!fn()){std::fprintf(stderr,"benchmark execution failed\n");std::exit(20);}
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();};
    for(unsigned i=0;i<240;++i) {
        double x,y;if(i%2){y=call(candidate);x=call(baseline);}else{x=call(baseline);y=call(candidate);}
        if(i>=40){a.push_back(x);b.push_back(y);}
    }
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    std::printf("{\"case\":\"%s\",\"baseline_p50_ms\":%.6f,\"candidate_p50_ms\":%.6f,\"baseline_p95_ms\":%.6f,\"candidate_p95_ms\":%.6f,\"model_executed\":false}\n",label,a[100],b[100],a[190],b[190]);
}
std::vector<float> Read(const char* path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};
    const auto bytes=f.tellg();if(bytes<=0||uint64_t(bytes)%4)return {};
    std::vector<float> v(size_t(bytes)/4);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),bytes);return v;
}
HV_TensorViewV1 View(const char* name,std::vector<float>& data,int cols) {
    HV_TensorViewV1 v{};v.struct_size=sizeof(v);v.api_version=1;v.name=name;v.element_type=1;
    v.rank=2;v.dimensions[0]=3024;v.dimensions[1]=cols;v.data=data.data();v.byte_count=data.size()*4;return v;
}
}
int main(int argc,char** argv) {
    using namespace humanvision::runtime;
    // Every half bit pattern and every vector lane/tail is checked on ARM64.
    std::vector<unsigned char> bytes(65536*2+1);std::vector<float> out(65536),ref(65536);
    for(unsigned i=0;i<65536;++i){uint16_t h=(i&0x7c00)==0x7c00?0:uint16_t(i);std::memcpy(bytes.data()+1+i*2,&h,2);}
    rknn::NativeOutputLayout layout;baseline_humanvision::runtime::rknn::NativeOutputLayout old;
    uint64_t count=65536;if(!layout.Configure(1,&count,1,&count,count,count*2)||!old.Configure(1,&count,1,&count,count,count*2))return 1;
    if(!layout.Decode(bytes.data()+1,count*2,out.data(),count*4)||!old.Decode(bytes.data()+1,count*2,ref.data(),count*4)||std::memcmp(out.data(),ref.data(),count*4))return 2;
#if defined(__aarch64__)
    // Unity/vendor code may alter floating-point control on its worker. Verify
    // widening for every half value under every rounding mode and flush flags.
    uint64_t fpcr=0;asm volatile("mrs %0, fpcr":"=r"(fpcr));
    bool exact=true;
    for(uint64_t rounding=0;rounding<4;++rounding) {
        const uint64_t altered=(fpcr&~(uint64_t(3)<<22))|(rounding<<22)|(uint64_t(1)<<24)|(uint64_t(1)<<19);
        asm volatile("msr fpcr, %0"::"r"(altered));
        exact=exact&&layout.Decode(bytes.data()+1,count*2,out.data(),count*4)&&!std::memcmp(out.data(),ref.data(),count*4);
    }
    asm volatile("msr fpcr, %0"::"r"(fpcr));if(!exact)return 21;
#endif
    for(uint64_t n=1;n<40;++n) {
        layout.Configure(1,&n,1,&n,n,n*2);old.Configure(1,&n,1,&n,n,n*2);
        for(unsigned i=0;i<n;++i){uint16_t h=uint16_t((i*1879)%0x7c00);std::memcpy(bytes.data()+1+i*2,&h,2);}
        if(!layout.Decode(bytes.data()+1,n*2,out.data(),n*4)||!old.Decode(bytes.data()+1,n*2,ref.data(),n*4)||std::memcmp(out.data(),ref.data(),n*4))return 3;
        for(unsigned lane=0;lane<n;++lane)for(uint16_t bad:{uint16_t(0x7c00),uint16_t(0xfc00),uint16_t(0x7e01)}) {
            uint16_t save;std::memcpy(&save,bytes.data()+1+lane*2,2);std::memcpy(bytes.data()+1+lane*2,&bad,2);
            if(layout.Decode(bytes.data()+1,n*2,out.data(),n*4))return 4;
            std::memcpy(bytes.data()+1+lane*2,&save,2);
        }
    }
    std::vector<float> values(350784,1.f);
    for(size_t n=1;n<=39;++n)for(size_t bad=0;bad<n;++bad) {
        values[bad]=INFINITY;if(AllFinite(values.data(),n))return 5;values[bad]=1;
    }
    Time("finite-full-output",[&]{observed+=std::all_of(values.begin(),values.end(),[](float v){return std::isfinite(v);});return true;},[&]{observed+=AllFinite(values.data(),values.size());return true;});
    count=350784;layout.Configure(1,&count,1,&count,count,count*2);old.Configure(1,&count,1,&count,count,count*2);
    bytes.resize(count*2);out.resize(count);ref.resize(count);
    for(unsigned i=0;i<count;++i){uint16_t h=uint16_t((i*37)%0x7c00)|uint16_t((i&1)<<15);std::memcpy(bytes.data()+i*2,&h,2);}
    Time("half-full-output",[&]{return old.Decode(bytes.data(),count*2,ref.data(),count*4);},[&]{return layout.Decode(bytes.data(),count*2,out.data(),count*4);});
    if(std::memcmp(out.data(),ref.data(),count*4))return 6;
    rknn::NativeInputTransform transform;baseline_humanvision::runtime::rknn::NativeInputTransform previous;
    const float mean[]={0.f,0.f,0.f},norm[]={1.f/255,1.f/255,1.f/255};transform.Configure(mean,norm);previous.Configure(mean,norm);
    std::vector<uint8_t> rgb(512*288*3);for(size_t i=0;i<rgb.size();++i)rgb[i]=uint8_t(i*37);
    for(unsigned channels:{3u,8u})for(unsigned stride:{512u,528u}) {
        const unsigned capacity=stride*288*channels*2+16;
        rknn::NativeInputLayout target;baseline_humanvision::runtime::rknn::NativeInputLayout source;
        target.Configure(512,288,stride,channels,capacity);source.Configure(512,288,stride,channels,capacity);
        std::vector<uint16_t> a(capacity/2+1,0xdead),b(capacity/2+1,0xdead);
        char label[80];std::snprintf(label,sizeof(label),"input-channels-%u-stride-%u",channels,stride);
        Time(label,[&]{return previous.Pack(rgb.data(),rgb.size(),source,a.data(),capacity);},[&]{return transform.Pack(rgb.data(),rgb.size(),target,b.data(),capacity);});
        if(a!=b||a.back()!=0xdead)return 7;
    }
    if(argc==3) {
        auto det=Read(argv[1]),points=Read(argv[2]);if(det.size()!=3024*65||points.size()!=3024*51)return 8;
        HV_TensorViewV1 views[]={View("out0",det,65),View("out1",points,51)};
        yolo::Decoder current(512,288);baseline_humanvision::runtime::yolo::Decoder original(512,288);
        yolo::Geometry g;baseline_humanvision::runtime::yolo::Geometry h;
        yolo::BuildGeometry(1024,576,512,288,g);baseline_humanvision::runtime::yolo::BuildGeometry(1024,576,512,288,h);
        HV_BodyObservationV1 a[8]{},b[8]{};float as[8]{},bs[8]{};uint32_t ac=0,bc=0;
        Time("real-seven-person-decoder",[&]{return original.Decode(views,2,h,123,a,as,8,ac);},[&]{return current.Decode(views,2,g,123,b,bs,8,bc);});
        if(ac!=bc||ac!=7||std::memcmp(a,b,ac*sizeof(a[0]))||std::memcmp(as,bs,ac*4))return 9;
    }
    std::puts("{\"oracle\":\"PASS\",\"all_half_patterns\":65536,\"rk3588_performance_verified\":false}");
    return 0;
}
