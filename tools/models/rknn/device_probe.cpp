// Offline RK3588 model probe. Uses the vendor ABI privately; no SDK/public ABI
// change and no CPU fallback. Repeated static input measures model throughput,
// not fresh per-person skeleton FPS. Run only with a separately verified model.
#include "rknn_api.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

using Clock = std::chrono::steady_clock;
static double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
static bool Integer(const char* text, unsigned& value) {
    if (!text || !*text || *text=='-') return false;
    errno=0; char* end=nullptr; const auto v=std::strtoul(text,&end,10);
    if (errno || !end || *end || v>1000000) return false;
    value=static_cast<unsigned>(v); return true;
}
static bool File(const char* path, std::vector<unsigned char>& bytes) {
    FILE* f=std::fopen(path,"rb"); if (!f) return false;
    if (std::fseek(f,0,SEEK_END)) { std::fclose(f); return false; }
    const auto size=std::ftell(f);
    if (size<1 || size>64*1024*1024 || std::fseek(f,0,SEEK_SET)) { std::fclose(f); return false; }
    bytes.resize(static_cast<size_t>(size));
    const bool ok=std::fread(bytes.data(),1,bytes.size(),f)==bytes.size();
    std::fclose(f); return ok;
}
static bool SaveNew(const std::string& path, const std::vector<float>& values) {
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600); if (fd<0) return false;
    const char* data=reinterpret_cast<const char*>(values.data());
    size_t remaining=values.size()*sizeof(float);
    while (remaining) {
        const auto written=write(fd,data,remaining);
        if (written<=0) { close(fd); return false; }
        remaining-=static_cast<size_t>(written); data+=written;
    }
    return close(fd)==0;
}
struct Api {
    void* library=nullptr;
    decltype(&rknn_init) init=nullptr;
    decltype(&rknn_destroy) destroy=nullptr;
    decltype(&rknn_query) query=nullptr;
    decltype(&rknn_set_core_mask) core=nullptr;
    decltype(&rknn_inputs_set) inputs=nullptr;
    decltype(&rknn_run) run=nullptr;
    decltype(&rknn_outputs_get) get=nullptr;
    decltype(&rknn_outputs_release) release=nullptr;
    ~Api() { if (library) dlclose(library); }
    bool Load(const char* path) {
        library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if (!library) { std::fprintf(stderr,"runtime load failed: %s\n",dlerror()); return false; }
#define SYMBOL(member, name) member=reinterpret_cast<decltype(member)>(dlsym(library,name)); if (!member) { std::fprintf(stderr,"runtime missing symbol: %s\n",name); return false; }
        SYMBOL(init,"rknn_init"); SYMBOL(destroy,"rknn_destroy"); SYMBOL(query,"rknn_query");
        SYMBOL(core,"rknn_set_core_mask"); SYMBOL(inputs,"rknn_inputs_set"); SYMBOL(run,"rknn_run");
        SYMBOL(get,"rknn_outputs_get"); SYMBOL(release,"rknn_outputs_release");
#undef SYMBOL
        return true;
    }
};
struct Context {
    Api& api; rknn_context value=0;
    ~Context() { if (value) api.destroy(value); }
};
struct Borrow {
    Api& api; rknn_context context; rknn_output* output; bool held=false;
    ~Borrow() { if (held) api.release(context,2,output); }
};
static bool Good(int code, const char* stage) {
    if (code==0) return true;
    std::fprintf(stderr,"RKNN %s failed: %d; no fallback\n",stage,code); return false;
}
static bool Shape(const rknn_tensor_attr& a, unsigned rows, unsigned columns) {
    return a.n_elems==rows*columns &&
        ((a.n_dims==3 && a.dims[0]==1 && a.dims[1]==rows && a.dims[2]==columns) ||
         (a.n_dims==2 && a.dims[0]==rows && a.dims[1]==columns));
}
static void TensorLog(const char* direction, const rknn_tensor_attr& a) {
    std::printf("tensor %s index=%u name=%.*s type=%d format=%d elements=%u stride_bytes=%u shape=",
        direction,a.index,int(RKNN_MAX_NAME_LEN),a.name,int(a.type),int(a.fmt),a.n_elems,a.size_with_stride);
    for (unsigned i=0;i<a.n_dims && i<RKNN_MAX_DIMS;++i) std::printf("%s%u",i?",":"",a.dims[i]);
    std::printf("\n");
}
struct Samples {
    std::vector<double> values;
    explicit Samples(unsigned count) { values.reserve(count); }
    void Print(const char* name) {
        if (values.empty()) { std::printf("\"%s\":null",name); return; }
        double sum=0; for (auto value:values) sum+=value;
        std::sort(values.begin(),values.end());
        const size_t p95=static_cast<size_t>(std::ceil(values.size()*.95))-1;
        std::printf("\"%s\":{\"count\":%zu,\"mean\":%.6f,\"p95\":%.6f,\"max\":%.6f}",
            name,values.size(),sum/values.size(),values[p95],values.back());
    }
};
static int Main(int argc, char** argv) {
    if (argc!=10) {
        std::fprintf(stderr,"usage: hv_rknn_probe runtime.so candidate.rknn input.rgb output-prefix width height warmup iterations core-mask(0|1|7)\n"); return 2;
    }
    unsigned w,h,warmup,count,mask;
    if (!Integer(argv[5],w) || !Integer(argv[6],h) || !Integer(argv[7],warmup) ||
        !Integer(argv[8],count) || !Integer(argv[9],mask) || w<32 || h<32 || w>960 || h>960 ||
        w%32 || h%32 || warmup>1000 || count<1 || count>10000 || (mask!=0 && mask!=1 && mask!=7)) {
        std::fprintf(stderr,"invalid argument: bounded shape/count or core selection\n"); return 2;
    }
    std::vector<unsigned char> image,model;
    if (!File(argv[3],image) || image.size()!=static_cast<size_t>(w)*h*3) {
        std::fprintf(stderr,"input byte count mismatch; expected RGB uint8 NHWC image\n"); return 3;
    }
    if (!File(argv[2],model)) { std::fprintf(stderr,"cannot read bounded model file\n"); return 3; }
    Api api; if (!api.Load(argv[1])) return 4;
    Context ctx{api};
    // SDK owns its worker. Do not use ASYNC_MASK, which returns the previous
    // frame, or COLLECT_PERF_MASK, which changes performance measurements.
    if (!Good(api.init(&ctx.value,model.data(),static_cast<uint32_t>(model.size()),0,nullptr),"init")) return 5;
    rknn_sdk_version version{};
    if (!Good(api.query(ctx.value,RKNN_QUERY_SDK_VERSION,&version,sizeof(version)),"version query")) return 5;
    std::printf("runtime_version=%.*s driver_version=%.*s requested_core_mask=%u init_flags=0\n",
        256,version.api_version,256,version.drv_version,mask);
    if (!Good(api.core(ctx.value,static_cast<rknn_core_mask>(mask)),"core selection")) return 5;
    rknn_input_output_num io{};
    if (!Good(api.query(ctx.value,RKNN_QUERY_IN_OUT_NUM,&io,sizeof(io)),"IO query") || io.n_input!=1 || io.n_output!=2) {
        std::fprintf(stderr,"expected one input and two raw outputs\n"); return 6;
    }
    rknn_tensor_attr input{};
    if (!Good(api.query(ctx.value,RKNN_QUERY_INPUT_ATTR,&input,sizeof(input)),"input query")) return 5;
    TensorLog("input",input);
    const bool nchw=input.n_dims==4 && input.dims[0]==1 && input.dims[1]==3 && input.dims[2]==h && input.dims[3]==w;
    const bool nhwc=input.n_dims==4 && input.dims[0]==1 && input.dims[1]==h && input.dims[2]==w && input.dims[3]==3;
    if ((!nchw && !nhwc) || input.n_elems!=w*h*3) { std::fprintf(stderr,"input tensor shape mismatch\n"); return 6; }
    const unsigned rows=w*h/64+w*h/256+w*h/1024;
    rknn_tensor_attr attrs[2]{};
    std::vector<float> buffers[2]={std::vector<float>(size_t(rows)*65),std::vector<float>(size_t(rows)*51)};
    rknn_output output[2]{};
    for (unsigned i=0;i<2;++i) {
        attrs[i].index=i;
        if (!Good(api.query(ctx.value,RKNN_QUERY_OUTPUT_ATTR,&attrs[i],sizeof(attrs[i])),"output query")) return 5;
        TensorLog("output",attrs[i]);
        if (!Shape(attrs[i],rows,i?51:65)) { std::fprintf(stderr,"raw output shape/order mismatch\n"); return 6; }
        output[i].index=i; output[i].want_float=1; output[i].is_prealloc=1;
        output[i].buf=buffers[i].data(); output[i].size=static_cast<uint32_t>(buffers[i].size()*sizeof(float));
    }
    rknn_input supplied{};
    supplied.index=0; supplied.buf=image.data(); supplied.size=static_cast<uint32_t>(image.size());
    supplied.type=RKNN_TENSOR_UINT8; supplied.fmt=RKNN_TENSOR_NHWC; supplied.pass_through=0;
    Samples inputMs(count),runMs(count),getMs(count),releaseMs(count),totalMs(count),nativeMs(count);
    for (unsigned iteration=0;iteration<warmup+count;++iteration) {
        Borrow borrow{api,ctx.value,output};
        const auto start=Clock::now();
        if (!Good(api.inputs(ctx.value,1,&supplied),"inputs_set")) return 5;
        const auto afterInput=Clock::now();
        if (!Good(api.run(ctx.value,nullptr),"run")) return 5;
        const auto afterRun=Clock::now();
        if (!Good(api.get(ctx.value,2,output,nullptr),"outputs_get")) return 5;
        borrow.held=true;
        const auto afterGet=Clock::now();
        for (unsigned i=0;i<2;++i) {
            if (output[i].buf!=buffers[i].data() || output[i].size!=buffers[i].size()*sizeof(float)) {
                std::fprintf(stderr,"output preallocated buffer contract mismatch\n"); return 6;
            }
            if (!std::all_of(buffers[i].begin(),buffers[i].end(),[](float v){return std::isfinite(v);})) {
                std::fprintf(stderr,"nonfinite raw output\n"); return 6;
            }
        }
        rknn_perf_run perf{};
        const int perfStatus=api.query(ctx.value,RKNN_QUERY_PERF_RUN,&perf,sizeof(perf));
        const int releaseStatus=api.release(ctx.value,2,output); borrow.held=false;
        if (!Good(releaseStatus,"outputs_release")) return 5;
        const auto end=Clock::now();
        if (iteration>=warmup) {
            inputMs.values.push_back(Ms(start,afterInput)); runMs.values.push_back(Ms(afterInput,afterRun));
            getMs.values.push_back(Ms(afterRun,afterGet)); releaseMs.values.push_back(Ms(afterGet,end));
            totalMs.values.push_back(Ms(start,end));
            if (perfStatus==0 && perf.run_duration>=0) nativeMs.values.push_back(perf.run_duration/1000.0);
        }
    }
    for (unsigned i=0;i<2;++i) if (!SaveNew(std::string(argv[4])+"-out"+std::to_string(i)+".fp32",buffers[i])) {
        std::fprintf(stderr,"cannot save new output; existing outputs are never overwritten\n"); return 7;
    }
    double sum=0; for (auto v:totalMs.values) sum+=v;
    std::printf("{\"scope\":\"repeated static input model benchmark; not SDK fresh skeleton FPS\",\"completed\":%u,\"warmup\":%u,\"core_mask_set_success\":%u,\"model_runs_per_second\":%.6f,\"timings_ms\":{",
        count,warmup,mask,count*1000.0/sum);
    inputMs.Print("inputs_set"); std::printf(","); runMs.Print("run_call"); std::printf(",");
    getMs.Print("outputs_get_wait"); std::printf(","); releaseMs.Print("output_check_perf_query_release"); std::printf(",");
    totalMs.Print("total"); std::printf(","); nativeMs.Print("vendor_perf_run"); std::printf("}}\n");
    return 0;
}
int main(int argc,char** argv) {
    try { return Main(argc,argv); }
    catch (...) { std::fprintf(stderr,"probe failed: exception; no fallback\n"); return 1; }
}
