#include "composition/source_binding.h"
#include "composition/session.h"
#include "composition/native_input_gpu_source.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
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
uint64_t NativeCaptureBytes(const HV_InputNativeFrameV1& frame) noexcept {
    const int64_t width=int64_t(frame.crop_right)-frame.crop_left;
    const int64_t height=int64_t(frame.crop_bottom)-frame.crop_top;
    return width>0&&height>0&&width<=4096&&height<=4096?uint64_t(width)*uint64_t(height)*4:0;
}
}
bool CopySourceConfig(const HV_RuntimeSourceV1& s,OwnedSourceConfig& out,std::string& error) {
    if(s.struct_size<sizeof(s)||s.api_version!=1||s.kind>HV_NATIVE_SOURCE_RTSP||(s.flags&~HV_NATIVE_SOURCE_LIVE_PREVIEW)||s.reserved||
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
NativeSourceBinding::NativeSourceBinding(PreviewMailbox::AllocationPolicy allocation):preview_(allocation){}
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
    config_=config;epoch_=epoch;
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
                self.capture_rgba_bytes_.store(NativeCaptureBytes(frame));
                self.preview_.AdmitSourceGeneration(self.config_.source_id,frame.frame.generation,self.epoch_);
                self.frames_.fetch_add(1);self.generation_.store(frame.frame.generation);
                self.frame_id_.store(frame.frame.sequence);
                self.source_width_.store(frame.source_width);self.source_height_.store(frame.source_height);
                self.analysis_width_.store(frame.frame.width);self.analysis_height_.store(frame.frame.height);
                if(auto* runtime=self.runtime_.load(std::memory_order_acquire))runtime->RecordSourceArrival(false);
            },mapping_.clock_id);
        if(!source->Initialize(error))return false;gpu_source_=std::move(source);
    }
    if(gpu_route&&!gpu_source_){error="Native GPU consumer adapter is unavailable";return false;}
    pixels_.resize(size_t(config.width)*config.height*4);
    // Native HD capture reveals its actual visible size on first completion.
    // Configure once before the workers; larger formats reconfigure on their
    // preview owner only, never on the Unity consumer.
    if(config_.flags&HV_NATIVE_SOURCE_LIVE_PREVIEW) {
        const auto preview_capacity=android_native_?PreviewMailbox::kCandidateMaximumBytes:static_cast<uint32_t>(pixels_.size());
        const auto configured=preview_.ConfigureCandidate(preview_capacity);
        if(configured!=PreviewMailbox::ConfigureResult::Ready)
            FailPreview(configured==PreviewMailbox::ConfigureResult::BudgetExceeded?
                "optional preview exceeds the unqualified1080p byte budget":"optional preview allocation failed; analysis remains active");
    }
    error.clear();return true;
}
bool NativeSourceBinding::StartIngestion(RuntimeSession& session,uint64_t epoch,std::string& error) {
    if(!handle_||running_.exchange(true)){error="Native source ingestion is unavailable or already active";return false;}
    runtime_.store(&session,std::memory_order_release);
    try {
        if(!gpu_source_)worker_=std::thread(&NativeSourceBinding::Run,this,&session,epoch);
        if(android_native_&&api_.request_capture&&api_.copy_capture)
            preview_worker_=std::thread(&NativeSourceBinding::PreviewLoop,this,epoch);
    }catch(const std::exception& exception) {
        // A second thread allocation can fail after ingestion started. Retire
        // that first owner while the caller's Session is still alive.
        StopIngestion();error=exception.what();return false;
    }catch(...) {StopIngestion();error="Cannot start the native source owners";return false;}
    error.clear();return true;
}
void NativeSourceBinding::SetError(const std::string& error) {
    errors_.fetch_add(1,std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(error_mutex_);error_=error;
}
std::string NativeSourceBinding::LastError() const {std::lock_guard<std::mutex> lock(error_mutex_);return error_;}
NativeSourceProgress NativeSourceBinding::Progress() const noexcept {
    const auto* gpu=dynamic_cast<const NativeInputGpuSource*>(gpu_source_.get());
    return {frames_.load(),generation_.load(),frame_id_.load(),errors_.load()+(gpu?gpu->ErrorCount():0),mapping_,
        source_width_.load(),source_height_.load(),analysis_width_.load(),analysis_height_.load()};
}
uint32_t NativeSourceBinding::State() const noexcept {
    if(const auto* gpu=dynamic_cast<const NativeInputGpuSource*>(gpu_source_.get()))
        if(gpu->ErrorCount())return HV_INPUT_FAILED;
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
        capture_rgba_bytes_.store(android_native_?NativeCaptureBytes(native):0);
        // Retire old preview identity before the asynchronous Session can admit
        // or publish this new generation, regardless of live-preview policy.
        preview_.AdmitSourceGeneration(config_.source_id,info.generation,epoch);
        session->RecordSourceArrival(false);
        std::string error;
        if(session->Submit(frame,envelope,error)) {
            frames_.fetch_add(1);generation_.store(info.generation);frame_id_.store(info.sequence);
            source_width_.store(android_native_?native.source_width:0);source_height_.store(android_native_?native.source_height:0);
            analysis_width_.store(info.width);analysis_height_.store(info.height);
            if(!android_native_)PublishCpuPreview(info,epoch);
        }else SetError(error);
    }
}
void NativeSourceBinding::StopIngestion() {
    running_.store(false,std::memory_order_release);wake_.notify_all();
    if(worker_.joinable())worker_.join();
    if(preview_worker_.joinable())preview_worker_.join();
    runtime_.store(nullptr,std::memory_order_release);
    preview_.Invalidate();
}
bool NativeSourceBinding::BuildPreviewInfo(const HV_InputFrameInfo& info,uint64_t epoch,HV_RuntimePreviewV1& out) const {
    int64_t timestamp=0;
    if(!info.sequence||!info.generation||!mapping_.Map(info,info.decoded_timestamp_us,timestamp)||
       !info.width||!info.height||info.width>4096||info.height>4096||
       uint64_t(info.stride_bytes)<uint64_t(info.width)*4||uint64_t(info.rgba_bytes)<uint64_t(info.stride_bytes)*info.height||
       info.rgba_bytes>4096u*4096u*4u||info.row_origin!=HV_INPUT_ROW_TOP_LEFT)return false;
    out={sizeof(out),1,info.width,info.height,info.stride_bytes,info.rgba_bytes,
        config_.source_id,info.generation,info.sequence,epoch,timestamp};return true;
}
void NativeSourceBinding::PublishCpuPreview(const HV_InputFrameInfo& info,uint64_t epoch) {
    if(preview_failed_||!(config_.flags&HV_NATIVE_SOURCE_LIVE_PREVIEW))return;
    const auto now=Now();if(now<next_cpu_preview_us_)return;next_cpu_preview_us_=now+200000;
    HV_RuntimePreviewV1 metadata{};if(!BuildPreviewInfo(info,epoch,metadata))return;
    if(!preview_.MatchesSourceGeneration(config_.source_id,info.generation,epoch))return;
    PreviewMailbox::CopyTicket ticket;
    // These bytes are already owned by the ingestion worker; no decoder or
    // analysis credit is retained for the optional copy.
    if(!preview_.TryBeginCopy(0,false,false,ticket))return;
    if(metadata.rgba_bytes>ticket.capacity) {
        preview_.FinishCopy(ticket,HV_RuntimePreviewV1{},true,0,false);
        FailPreview("optional preview exceeds its configured buffer capacity");return;
    }
    const auto started=Now();std::memcpy(ticket.data,pixels_.data(),metadata.rgba_bytes);
    preview_.FinishCopy(ticket,metadata,true,static_cast<uint64_t>(Now()-started));
}
int NativeSourceBinding::CopyPreview(uint64_t after,void* data,uint32_t capacity,HV_RuntimePreviewV1* info) {
    return preview_.CopyLatest(after,data,capacity,info);
}
int NativeSourceBinding::RequestSnapshot(uint64_t* request) {
    if(!request)return HV_ERR_INVALID_ARGUMENT;*request=0;
    // The current Windows Input API only supplies resized analysis RGBA; it
    // cannot establish an original-color snapshot contract.
    if(!android_native_||!api_.request_capture||!api_.copy_capture||preview_failed_)return HV_CONTROL_UNSUPPORTED;
    const auto result=preview_.RequestSnapshot(request);
    if(result==0)wake_.notify_all();
    return result;
}
int NativeSourceBinding::CopySnapshot(uint64_t request,void* data,uint32_t capacity,HV_RuntimePreviewV1* info) {
    return preview_.CopySnapshot(request,data,capacity,info);
}
int NativeSourceBinding::CancelSnapshot(uint64_t request) {return preview_.CancelSnapshot(request);}
std::string NativeSourceBinding::PreviewDiagnostics() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return "preview: native-owned copies="+std::to_string(preview_.Published())+
        " skipped="+std::to_string(preview_.Skipped())+" live_limit_fps=5 live_suspended="+
        (preview_.BudgetExceeded()?"1":"0")+" budget=cpu-copy-20ms/request-ready-200ms-unqualified"+
        (android_native_?(preview_failed_?" exact=unsupported-preview-failure":" exact=next-accepted-original"):" exact=unsupported-original-geometry")+
        (preview_error_[0]?" error="+std::string(preview_error_.data()):"");
}
void NativeSourceBinding::FailPreview(const char* error) noexcept {
    preview_failed_=true;preview_.SuspendLive();
    try {
        if(const auto request=preview_.PendingSnapshot())preview_.FailSnapshot(request,HV_CONTROL_UNSUPPORTED);
        std::lock_guard<std::mutex> lock(error_mutex_);
        size_t count=0;while(error&&count+1<preview_error_.size()&&error[count])++count;
        if(count)std::memcpy(preview_error_.data(),error,count);preview_error_[count]=0;
    }catch(...) {} // Optional failure reporting must not terminate an owner thread.
}
void NativeSourceBinding::CheckSnapshot(RuntimeSession& session) {
    uint64_t request=0;HV_RuntimePreviewV1 info{};
    if(!preview_.SnapshotCandidate(request,info))return;
    if(request!=snapshot_check_request_||info.frame_id!=snapshot_check_frame_) {
        snapshot_check_request_=request;snapshot_check_frame_=info.frame_id;snapshot_check_cursor_=0;
    }
    bool passed=false;ObservationEntry entry;
    for(uint32_t n=0;n<64;++n){const auto read=session.ReadObservations(snapshot_check_cursor_,&entry,1);if(!read.written)break;snapshot_check_cursor_=read.next_cursor;
        if(entry.kind!=ObservationKind::Body||entry.envelope.source_id!=info.source_id||entry.envelope.config_epoch!=info.config_epoch)continue;
        if(entry.envelope.generation==info.generation&&entry.envelope.frame_id==static_cast<int64_t>(info.frame_id)) {
            preview_.ConfirmSnapshot(request,info.source_id,info.generation,info.frame_id,info.config_epoch);return;
        }
        if(entry.envelope.generation>info.generation||(entry.envelope.generation==info.generation&&
           entry.envelope.frame_id>static_cast<int64_t>(info.frame_id)))passed=true;
    }
    // The capture was overwritten before inference admission, or its result
    // expired from the journal. Retry this request's next exact image; never
    // substitute an arbitrary current image.
    if(passed)preview_.RejectSnapshotCandidate(request);
}
void NativeSourceBinding::PreviewLoop(uint64_t epoch) noexcept {
    try {PreviewLoopCore(epoch);}
    catch(const std::bad_alloc&) {FailPreview("optional preview allocation failed; analysis remains active");}
    catch(const std::exception& error) {FailPreview(error.what());}
    catch(...) {FailPreview("optional preview owner failed; analysis remains active");}
}
void NativeSourceBinding::PreviewLoopCore(uint64_t epoch) {
    uint64_t native_request=0,capture_snapshot=0;int64_t requested_us=0,next_live_us=0,last_live_us=0;
    PreviewMailbox::CopyTicket ticket;
    struct CpuReservationRetirement {
        PreviewMailbox& mailbox;PreviewMailbox::CopyTicket& ticket;
        ~CpuReservationRetirement(){if(ticket.token)try{mailbox.FinishCopy(ticket,HV_RuntimePreviewV1{},true,0,false);}catch(...) {}}
    } reservation_retirement{preview_,ticket};
    auto retire_cpu_reservation=[&] {
        // No GPU command writes this mailbox target. Input's pending capture
        // owns its own staging and is retired separately by Input Close/Release.
        if(ticket.token){preview_.FinishCopy(ticket,HV_RuntimePreviewV1{},true,0,false);ticket={};}
    };
    while(running_.load(std::memory_order_acquire)) {
        const auto now=Now();auto* session=runtime_.load(std::memory_order_acquire);
        preview_.ExpireSnapshot();
        if(session)CheckSnapshot(*session);
        if(native_request) {
            HV_InputFrameInfo info{sizeof(info),1};const auto probe=api_.copy_capture(handle_,native_request,nullptr,0,&info);
            if(probe==HV_INPUT_BUFFER_TOO_SMALL) {
                HV_RuntimePreviewV1 metadata{};
                if(!BuildPreviewInfo(info,epoch,metadata)){retire_cpu_reservation();native_request=0;FailPreview("native capture metadata/clock is invalid; capture staging remains Input-owned until source retirement");}
                else {
                    if(!preview_.MatchesSourceGeneration(metadata.source_id,metadata.generation,metadata.config_epoch)) {
                        // Drain only this old ready capture; authoritative source
                        // admission already invalidated its logical image IDs.
                        if(ticket.data&&info.rgba_bytes<=ticket.capacity)api_.copy_capture(handle_,native_request,ticket.data,ticket.capacity,&info);
                        if(capture_snapshot)preview_.CancelSnapshot(capture_snapshot);
                        retire_cpu_reservation();native_request=0;continue;
                    }
                    if(info.rgba_bytes>ticket.capacity) {
                        retire_cpu_reservation();const auto configured=preview_.ConfigureCandidate(info.rgba_bytes);
                        if(configured==PreviewMailbox::ConfigureResult::Busy){std::unique_lock<std::mutex> lock(wait_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(10));continue;}
                        if(configured!=PreviewMailbox::ConfigureResult::Ready) {
                            native_request=0;FailPreview(configured==PreviewMailbox::ConfigureResult::BudgetExceeded?
                                "optional capture exceeds the unqualified1080p byte budget":"optional preview growth allocation failed; analysis remains active");continue;
                        }
                    }
                    if(!ticket.token&&!preview_.TryBeginCopy(0,false,false,ticket,capture_snapshot!=0)){std::unique_lock<std::mutex> lock(wait_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(10));continue;}
                    const auto copy_started=Now();const auto copied=api_.copy_capture(handle_,native_request,ticket.data,ticket.capacity,&info);
                    const auto copy_us=static_cast<uint64_t>(Now()-copy_started);
                    if(copied==HV_INPUT_OK) {
                        if(capture_snapshot&&capture_snapshot==preview_.PendingSnapshot())preview_.StoreSnapshotCandidate(capture_snapshot,metadata,ticket.data,metadata.rgba_bytes);
                        preview_.FinishCopy(ticket,metadata,true,copy_us,(config_.flags&HV_NATIVE_SOURCE_LIVE_PREVIEW)!=0);
                        if(Now()-requested_us>200000)preview_.SuspendLive();
                        ticket={};native_request=0;next_live_us=Now()+200000;
                    }else if(copied!=HV_INPUT_NO_FRAME&&copied!=HV_INPUT_BUSY){retire_cpu_reservation();native_request=0;FailPreview("native capture copy failed");}
                }
            }else if(probe!=HV_INPUT_NO_FRAME&&probe!=HV_INPUT_BUSY){retire_cpu_reservation();native_request=0;
                if(probe==HV_INPUT_INVALID){if(capture_snapshot)preview_.CancelSnapshot(capture_snapshot);}
                else FailPreview("native capture preparation failed");}
        }else {
            uint64_t candidate_request=0;HV_RuntimePreviewV1 candidate{};
            const auto snapshot=preview_.PendingSnapshot();
            const bool exact=snapshot&&!preview_.SnapshotCandidate(candidate_request,candidate);
            bool live=(config_.flags&HV_NATIVE_SOURCE_LIVE_PREVIEW)&&!preview_.BudgetExceeded()&&now>=next_live_us;
            // A normal asynchronous frame gap is not pressure. If more than
            // three admitted frames are outstanding, reduce optional previews
            // to one per second; the capture still attaches only after the
            // image owner admits and submits the next analysis frame first.
            if(live&&session){const auto stats=session->StatsV2();const auto latest=frame_id_.load();
                if(latest>uint64_t(std::max<int64_t>(0,stats.source_frame_id))+3&&now-last_live_us<1000000)live=false;}
            if(!preview_failed_&&frame_id_.load()&&(exact||live)) {
                const auto expected_bytes=capture_rgba_bytes_.load();
                if(!expected_bytes||expected_bytes>PreviewMailbox::kCandidateMaximumBytes) {
                    FailPreview(expected_bytes?"optional capture exceeds the unqualified1080p byte budget":"optional capture crop geometry is unavailable");continue;
                }
                const auto configured=preview_.ConfigureCandidate(PreviewMailbox::kCandidateMaximumBytes);
                if(configured==PreviewMailbox::ConfigureResult::Busy){std::unique_lock<std::mutex> lock(wait_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(10));continue;}
                if(configured!=PreviewMailbox::ConfigureResult::Ready){FailPreview("optional preview allocation failed; analysis remains active");continue;}
                if(!preview_.TryBeginCopy(0,false,false,ticket,exact)) {
                    std::unique_lock<std::mutex> lock(wait_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(10));continue;
                }
                uint64_t id=0;const auto result=api_.request_capture(handle_,&id);
                if(result==HV_INPUT_OK){native_request=id;capture_snapshot=exact?snapshot:0;requested_us=now;if(live)last_live_us=now;}
                else {retire_cpu_reservation();next_live_us=now+200000;}
            }
        }
        std::unique_lock<std::mutex> lock(wait_mutex_);wake_.wait_for(lock,std::chrono::milliseconds(10),[this]{return !running_.load();});
    }
    retire_cpu_reservation();
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
