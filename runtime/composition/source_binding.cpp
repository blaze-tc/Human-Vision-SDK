#include "composition/source_binding.h"
#include "composition/session.h"
#include "composition/native_input_gpu_source.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace humanvision::runtime {
namespace {
int64_t Now() noexcept {return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
bool String(const char* p,size_t limit,std::string& out) {
    if(!p){out.clear();return true;}
    size_t n=0;while(n<=limit&&p[n])++n;
    if(n>limit)return false;out.assign(p,n);return true;
}
std::atomic<uint64_t> next_source{1};
}
bool CopySourceConfig(const HV_RuntimeSourceV1& s,OwnedSourceConfig& out,std::string& error) {
    if(s.struct_size<sizeof(s)||s.api_version!=1||s.kind>HV_NATIVE_SOURCE_RTSP||s.flags||s.reserved||
       s.mirror_x>1||s.transport_tcp>1||s.rotation_degrees%90||s.rotation_degrees>=360||
       (s.kind&&(!s.url_utf8||!*s.url_utf8||!s.width||!s.height||s.width>4096||s.height>4096))) {
        error="Invalid native source configuration";return false;
    }
    OwnedSourceConfig copy;
    if(!String(s.library_utf8,4096,copy.library)||!String(s.url_utf8,4096,copy.url)||
       !String(s.hardware_avc_codec_utf8,256,copy.hardware_avc_codec)) {
        error="Native source string exceeds its bounded contract";return false;
    }
    if(s.kind&&copy.url.rfind("rtsp://",0)!=0) {
        error="Native source currently supports RTSP URLs only";return false;
    }
    copy.kind=s.kind;copy.flags=s.flags;copy.width=s.width;copy.height=s.height;
    copy.source_id=s.source_id?s.source_id:next_source.fetch_add(1);
    copy.timeout_ms=s.timeout_ms?s.timeout_ms:5000;
    copy.reconnect_delay_ms=s.reconnect_delay_ms?s.reconnect_delay_ms:1000;
    copy.transport_tcp=s.transport_tcp;copy.rotation_degrees=s.rotation_degrees;copy.mirror_x=s.mirror_x;
    if(copy.timeout_ms<100||copy.timeout_ms>60000||copy.reconnect_delay_ms>60000) {error="Source timeout must be between 100 and 60000 milliseconds";return false;}
    out=std::move(copy);error.clear();return true;
}
bool BuildInputClockMapping(const HV_InputClockInfo& c,int64_t before,int64_t after,InputClockMapping& out) noexcept {
    out={};
    if(c.size<sizeof(c)||c.version!=1||!c.clock_id||c.clock_domain!=HV_INPUT_CLOCK_NATIVE_MONOTONIC||
       c.now_us<0||before<0||after<before)return false;
    const int64_t midpoint=before+(after-before)/2;
    out.clock_id=c.clock_id;out.offset_us=midpoint-c.now_us;
    out.uncertainty_us=(after-before+1)/2;out.valid=true;return true;
}
bool InputClockMapping::Map(const HV_InputFrameInfo& f,int64_t input,int64_t& output) const noexcept {
    if(!valid||f.clock_id!=clock_id||f.clock_domain!=HV_INPUT_CLOCK_NATIVE_MONOTONIC||input<0||
       (offset_us>0&&input>std::numeric_limits<int64_t>::max()-offset_us)||
       (offset_us<0&&input<std::numeric_limits<int64_t>::min()-offset_us))return false;
    output=input+offset_us;return output>=0;
}
FrameEnvelope EnvelopeForNativeFrame(const HV_InputFrameInfo& f,const InputClockMapping& clock,
                                     uint64_t source,uint64_t epoch) noexcept {
    FrameEnvelope e;e.source_id=source;e.generation=f.generation;e.frame_id=static_cast<int64_t>(f.sequence);
    e.config_epoch=epoch;e.width=static_cast<int32_t>(f.width);e.height=static_cast<int32_t>(f.height);
    e.crop={0,0,e.width,e.height};e.format=HV_PIXEL_RGBA32;e.plane_count=1;
    e.planes[0]={0,static_cast<int32_t>(f.stride_bytes),4};
    e.color_matrix=FrameColorMatrix::kRgb;e.color_range=FrameColorRange::kFull;
    e.decoded={f.decoded_timestamp_us,f.clock_id,FrameClockDomain::kInputMonotonic,FrameTimeSource::kDecoded,f.decoded_timestamp_us>=0};
    e.received={f.received_timestamp_us,f.clock_id,FrameClockDomain::kInputMonotonic,FrameTimeSource::kReceived,f.received_timestamp_us>=0};
    e.presentation={f.presentation_timestamp_us,0,FrameClockDomain::kMediaPresentation,FrameTimeSource::kPresentation,f.pts_valid!=0};
    int64_t mapped=0;
    if(clock.Map(f,f.decoded_timestamp_us,mapped))
        e.source_timestamp={mapped,1,FrameClockDomain::kRuntimeMonotonic,FrameTimeSource::kDecoded,true};
    e.completion=FrameCompletion::kReady;return e;
}
NativeSourceBinding::~NativeSourceBinding(){StopIngestion();Close();}
bool NativeSourceBinding::Load(const std::string& name,std::string& error) {
#if defined(_WIN32)
    auto path=std::filesystem::u8path(name);
    if(!path.is_absolute()) {error="Native Input DLL path must be absolute";return false;}
    // LOAD_WITH_ALTERED_SEARCH_PATH resolves transitive DLLs from the native
    // directory only when the absolute name uses Windows path separators.
    path.make_preferred();
    library_=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!library_) {error="Cannot load native Input DLL: "+std::to_string(GetLastError());return false;}
    auto symbol=[this](const char* n){return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library_),n));};
#else
    library_=dlopen(name.c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!library_) {const char* e=dlerror();error=std::string("Cannot load native Input library: ")+(e?e:"unknown");return false;}
    auto symbol=[this](const char* n){return dlsym(library_,n);};
#endif
#define INPUT_SYMBOL(field,name) api_.field=reinterpret_cast<decltype(api_.field)>(symbol(name))
    INPUT_SYMBOL(open,"HV_Input_Open");INPUT_SYMBOL(close,"HV_Input_Close");
    INPUT_SYMBOL(release,"HV_Input_Release");INPUT_SYMBOL(state,"HV_Input_GetState");
    INPUT_SYMBOL(error,"HV_Input_GetLastError");INPUT_SYMBOL(poll,"HV_Input_PollFrame");
    INPUT_SYMBOL(copy,"HV_Input_CopyRgba");INPUT_SYMBOL(clock,"HV_Input_QueryClock");
    INPUT_SYMBOL(open_native,"HV_Input_OpenNativeV1");INPUT_SYMBOL(device,"HV_Input_GetNativeDeviceV1");
    INPUT_SYMBOL(pool,"HV_Input_GetNativePoolV1");INPUT_SYMBOL(acquire,"HV_Input_AcquireNativeFrameV1");
    INPUT_SYMBOL(wait,"HV_Input_WaitNativeFrameV1");INPUT_SYMBOL(copy_native,"HV_Input_CopyNativeRgbaV1");
    INPUT_SYMBOL(release_native,"HV_Input_ReleaseNativeFrameV1");
    INPUT_SYMBOL(request_capture,"HV_Input_RequestNativeCaptureV1");INPUT_SYMBOL(copy_capture,"HV_Input_CopyNativeCaptureV1");
#undef INPUT_SYMBOL
    if(!api_.open||!api_.close||!api_.release||!api_.state||!api_.error||!api_.poll||!api_.copy||!api_.clock) {
        error="Native Input library lacks the required versioned input contract";return false;
    }
    return true;
}
bool NativeSourceBinding::Open(const OwnedSourceConfig& config,bool gpu_route,uint64_t epoch,std::string& error) {
    if(handle_||library_){error="Native source is already open";return false;}
    config_=config;
    std::string library=config.library;
#if defined(_WIN32)
    if(library.empty()){error="An absolute native Input DLL path is required";return false;}
#else
    if(library.empty())library="libhumanvision_input.so";
#endif
    if(!Load(library,error))return false;
    HV_InputClockInfo clock{sizeof(clock),1};const auto before=Now();
    const auto clock_result=api_.clock(&clock);const auto after=Now();
    if(clock_result!=HV_INPUT_OK||!BuildInputClockMapping(clock,before,after,mapping_)) {
        error="Native Input monotonic clock could not be mapped";return false;
    }
    HV_InputOptions options{sizeof(options),1,config_.url.c_str(),config.width,config.height,
        config.timeout_ms,config.reconnect_delay_ms,config.transport_tcp};
#if defined(__ANDROID__)
    android_native_=true;
    if(config_.hardware_avc_codec.empty()||!api_.open_native||!api_.wait||!api_.copy_native||!api_.release_native) {
        error="Independent Android input requires a selected hardware AVC codec and native output API";return false;
    }
    HV_InputNativeOptionsV1 native{sizeof(native),1,options,config_.hardware_avc_codec.c_str(),
        config.width,config.height,config.rotation_degrees,config.mirror_x,gpu_route?0u:1u};
    char message[1024]{};
    if(api_.open_native(&native,&handle_,message,sizeof(message))!=HV_INPUT_OK) {
        error=message[0]?message:"Native Vulkan input failed to open";return false;
    }
#else
    if(gpu_route){error="This native source has no supported GPU consumer route on this platform";return false;}
    if(config.rotation_degrees||config.mirror_x) {
        error="Native Windows source rotation/mirroring is not implemented; use a compatible source";return false;
    }
    if(api_.open(&options,&handle_)!=HV_INPUT_OK){error="Native CPU input failed to open";return false;}
#endif
    if(gpu_route) {
        auto source=std::make_unique<NativeInputGpuSource>(handle_,
            NativeInputGpuApi{api_.device,api_.pool,api_.acquire,api_.release_native},mapping_.offset_us,
            config_.source_id,epoch,this,[](void* context,const HV_InputNativeFrameV1& frame) {
                auto& self=*static_cast<NativeSourceBinding*>(context);
                self.frames_.fetch_add(1);self.generation_.store(frame.frame.generation);
                self.frame_id_.store(frame.frame.sequence);
                if(auto* runtime=self.runtime_.load(std::memory_order_acquire))runtime->RecordSourceArrival(false);
            });
        if(!source->Initialize(error))return false;gpu_source_=std::move(source);
    }
    if(gpu_route&&!gpu_source_){error="Native GPU consumer adapter is unavailable";return false;}
    pixels_.resize(size_t(config.width)*config.height*4);
    error.clear();return true;
}
bool NativeSourceBinding::StartIngestion(RuntimeSession& session,uint64_t epoch,std::string& error) {
    if(!handle_||running_.exchange(true)){error="Native source ingestion is unavailable or already active";return false;}
    runtime_.store(&session,std::memory_order_release);
    if(gpu_source_){error.clear();return true;}
    worker_=std::thread(&NativeSourceBinding::Run,this,&session,epoch);error.clear();return true;
}
void NativeSourceBinding::SetError(const std::string& error) {
    errors_.fetch_add(1,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(error_mutex_);error_=error;
}
std::string NativeSourceBinding::LastError() const {std::lock_guard<std::mutex> lock(error_mutex_);return error_;}
NativeSourceProgress NativeSourceBinding::Progress() const noexcept {
    return {frames_.load(),generation_.load(),frame_id_.load(),errors_.load(),mapping_};
}
uint32_t NativeSourceBinding::State() const noexcept {
    uint32_t state=HV_INPUT_STOPPED;if(handle_&&api_.state)api_.state(handle_,&state);return state;
}
void NativeSourceBinding::Run(RuntimeSession* session,uint64_t epoch) {
    uint64_t after=0;
    while(running_.load(std::memory_order_acquire)) {
        HV_InputFrameInfo info{sizeof(info),1};
        HV_InputNativeFrameV1 native{};native.size=sizeof(native);native.version=1;
        const int acquired=android_native_?api_.wait(handle_,after,50,&native):api_.poll(handle_,after,&info);
        if(acquired==HV_INPUT_NO_FRAME) {
            if(State()==HV_INPUT_FAILED) {char message[1024]{};api_.error(handle_,message,sizeof(message));
                SetError(message[0]?message:"Native Input entered its failed state");break;}
            if(!android_native_) {std::unique_lock<std::mutex> lock(wait_mutex_);
                wake_.wait_for(lock,std::chrono::milliseconds(4),[this]{return !running_.load();});}
            continue;
        }
        if(acquired!=HV_INPUT_OK){SetError("Native Input failed to acquire a frame");break;}
        if(android_native_)info=native.frame;
        const bool layout=info.size>=sizeof(info)&&info.version==1&&info.width&&info.height&&
            info.width<=config_.width&&info.height<=config_.height&&info.stride_bytes>=uint64_t(info.width)*4&&
            info.rgba_bytes<=pixels_.size()&&info.rgba_bytes>=uint64_t(info.stride_bytes)*info.height&&
            info.sequence<=uint64_t(std::numeric_limits<int64_t>::max())&&info.row_origin==HV_INPUT_ROW_TOP_LEFT;
        int copied=HV_INPUT_INVALID;
        if(layout)copied=android_native_?api_.copy_native(handle_,native.slot,info.generation,info.sequence,pixels_.data(),static_cast<uint32_t>(pixels_.size())):
            api_.copy(handle_,info.sequence,pixels_.data(),static_cast<uint32_t>(pixels_.size()),&info);
        if(android_native_) {
#if !defined(_WIN32)
            if(native.producer_sync_fd>=0)::close(native.producer_sync_fd);
#endif
            // CPU copy observed the producer fence and no consumer GPU work was submitted.
            const int released=api_.release_native(handle_,native.slot,native.frame.generation,native.frame.sequence);
            if(released!=HV_INPUT_OK){SetError("Native Input frame lease could not retire");break;}
        }
        after=info.sequence;
        if(copied==HV_INPUT_NO_FRAME)continue;
        if(!layout||copied!=HV_INPUT_OK){SetError("Native Input RGBA frame contract/copy failed");continue;}
        auto envelope=EnvelopeForNativeFrame(info,mapping_,config_.source_id,epoch);
        if(!envelope.source_timestamp.valid){SetError("Native Input frame clock identity changed without remapping");continue;}
        HV_VideoFrame frame{sizeof(frame),static_cast<int32_t>(info.width),static_cast<int32_t>(info.height),
            static_cast<int32_t>(info.stride_bytes),HV_PIXEL_RGBA32,static_cast<int64_t>(info.sequence),
            envelope.source_timestamp.microseconds,pixels_.data(),static_cast<int32_t>(info.rgba_bytes)};
        session->RecordSourceArrival(false);
        std::string error;
        if(session->Submit(frame,envelope,error)) {
            frames_.fetch_add(1);generation_.store(info.generation);frame_id_.store(info.sequence);
        }else SetError(error);
    }
}
void NativeSourceBinding::StopIngestion() {
    running_.store(false,std::memory_order_release);wake_.notify_all();
    if(worker_.joinable())worker_.join();
}
void NativeSourceBinding::Close() {
    if(handle_){api_.close(handle_);for(;;){const auto released=api_.release(handle_);
        if(released==HV_INPUT_OK){handle_=nullptr;break;}
        if(released!=HV_INPUT_BUSY)SetError("Native Input retirement failed; resources remain owned until process restart");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
    gpu_source_.reset();
    if(library_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(library_));
#else
        dlclose(library_);
#endif
        library_=nullptr;
    }
}
}
