#include "composition/runtime_controller.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace humanvision::runtime {
namespace {
int64_t Now() {return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
void Text(char* out,size_t capacity,const std::string& value) {
    if(out&&capacity){const auto n=std::min(capacity-1,value.size());std::memcpy(out,value.data(),n);out[n]=0;}
}
bool Bounded(const char* value,size_t max,std::string& out) {
    if(!value)return false;size_t n=0;while(n<=max&&value[n])++n;
    if(!n||n>max)return false;out.assign(value,n);return true;
}
}
bool CopyRuntimeConfig(const HV_RuntimeControllerConfigV1& c,OwnedRuntimeConfig& out,std::string& error) {
    if(c.struct_size<sizeof(c)||c.api_version!=1||c.runtime.struct_size<sizeof(c.runtime)||
       c.runtime.api_version!=HV_API_VERSION_040||c.runtime.reserved||c.runtime.max_people<1||
       c.runtime.max_people>HV_MAX_PEOPLE||c.execution_policy>HV_EXECUTION_INDEPENDENT_REQUIRED||
       c.retirement_timeout_ms>60000) {error="Invalid deferred runtime configuration";return false;}
    OwnedRuntimeConfig copy;
    if(!Bounded(c.runtime.runtime_root_utf8,4096,copy.root)||!Bounded(c.runtime.profile_id_utf8,256,copy.profile)) {
        error="Runtime root/profile is missing or exceeds its bounded contract";return false;
    }
    copy.capacity=c.runtime.max_people;copy.policy=c.execution_policy;
    copy.retirement_timeout_ms=c.retirement_timeout_ms?c.retirement_timeout_ms:5000;
    out=std::move(copy);error.clear();return true;
}
std::unique_ptr<RuntimeController> RuntimeController::Create(const HV_RuntimeControllerConfigV1& config,
    std::string& error,SessionFactory factory) {
    OwnedRuntimeConfig copy;if(!CopyRuntimeConfig(config,copy,error))return {};
    return std::unique_ptr<RuntimeController>(new RuntimeController(std::move(copy),std::move(factory)));
}
RuntimeController::RuntimeController(OwnedRuntimeConfig config,SessionFactory factory)
    :config_(std::move(config)),factory_(std::move(factory)) {
    if(!factory_)factory_=[](const OwnedRuntimeConfig& c,GpuConsumerSource* source,std::shared_ptr<ObservationJournal> journal,std::string& error) {
        auto session=std::make_unique<RuntimeSession>(std::move(journal));
        session->EnablePreparedImages(c.policy!=HV_EXECUTION_COMPATIBLE);
        if(!session->Start(std::filesystem::u8path(c.root),c.profile,c.capacity,error,nullptr,source))return std::unique_ptr<RuntimeSession>{};
        return session;
    };
    control_=std::thread(&RuntimeController::Run,this);
}
RuntimeController::~RuntimeController(){if(control_.joinable())control_.join();}
int RuntimeController::Request(Operation operation,uint64_t* id,const OwnedRuntimeConfig* config,const OwnedSourceConfig* source) {
    if(!id)return HV_ERR_INVALID_ARGUMENT;*id=0;
    std::lock_guard<std::mutex> lock(mutex_);
    if(operation==Operation::Close&&close_requested_){*id=close_request_;return 0;}
    if(close_requested_||retired_)return HV_ERR_NOT_INITIALIZED;
    if(commands_.size()>=16&&operation!=Operation::Close)return HV_CONTROL_BUSY;
    if(operation==Operation::Close) {
        close_requested_=true;
        for(const auto& pending:commands_) {
            auto& status=requests_[pending.id%requests_.size()];status.status=HV_REQUEST_CANCELLED;
            status.result=HV_ERR_NOT_INITIALIZED;Text(status.message,sizeof(status.message),"Superseded by close");
        }
        commands_.clear();
    }
    const auto number=++next_request_;
    Command command{operation,number,{},{}};if(config)command.config=*config;if(source)command.source=*source;
    commands_.push_back(std::move(command));
    auto& record=requests_[number%requests_.size()];record={};record.struct_size=sizeof(record);
    record.api_version=1;record.request_id=number;record.status=HV_REQUEST_PENDING;record.state=VisibleState();
    const bool retirement=operation==Operation::Pause||operation==Operation::Stop||operation==Operation::Close||
       operation==Operation::Reconfigure||operation==Operation::Source;
    request_deadlines_[number%requests_.size()]=retirement?Now()+int64_t(config_.retirement_timeout_ms)*1000:0;
    if(retirement){admission_=false;if(!retirement_requested_us_)retirement_requested_us_=Now();}
    if(operation==Operation::Close)close_request_=number;
    *id=number;wake_.notify_one();return 0;
}
uint32_t RuntimeController::VisibleState() const {
    if(retirement_requested_us_&&Now()-retirement_requested_us_>int64_t(config_.retirement_timeout_ms)*1000)
        return HV_CONTROL_ERROR_RETIRING;
    return state_;
}
void RuntimeController::Retire() {
    std::unique_ptr<RuntimeSession> session;std::unique_ptr<NativeSourceBinding> source;
    {std::lock_guard<std::mutex> lock(mutex_);admission_=false;session=std::move(session_);source=std::move(source_);}
    // Never hold the publication/query mutex while joining or waiting on a driver.
    if(source)source->StopIngestion();
    if(session){session->Stop();std::lock_guard<std::mutex> lock(mutex_);runtime_errors_+=session->ErrorCount();}
    session.reset(); // GPU imports retain their AHB refs until here
    if(source)source->Close();
}
bool RuntimeController::Start(std::string& error) {
    OwnedRuntimeConfig config;OwnedSourceConfig source_config;uint64_t epoch;
    {std::lock_guard<std::mutex> lock(mutex_);config=config_;source_config=source_config_;epoch=++epoch_;fallback_.clear();}
    if(config.policy==HV_EXECUTION_INDEPENDENT_REQUIRED&&!source_config.kind) {
        error="IndependentRequired needs a supported native source; Unity input is not an autonomous fallback";return false;
    }
    std::unique_ptr<NativeSourceBinding> source;
    if(source_config.kind) {
        source=std::make_unique<NativeSourceBinding>();
        if(!source->Open(source_config,detail::IsAndroidGpuProfile(config.profile),epoch,error))return false;
    }
    if(!source&&config.policy==HV_EXECUTION_INDEPENDENT_PREFERRED) {
        std::lock_guard<std::mutex> lock(mutex_);fallback_="No native source was selected; compatible Unity input remains active";
    }
    auto session=factory_(config,source?source->GpuSource():nullptr,journal_,error);
    if(!session)return false;
    if(source&&!source->StartIngestion(*session,epoch,error))return false;
    {std::lock_guard<std::mutex> lock(mutex_);
        if(!close_requested_){session_=std::move(session);source_=std::move(source);admission_=commands_.empty();return true;}}
    if(source)source->StopIngestion();session.reset();if(source)source->Close();
    error="Runtime start cancelled by close";return false;
}
void RuntimeController::Run() {
    for(;;) {
        Command command;
        {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[this]{return !commands_.empty();});
            command=std::move(commands_.front());commands_.pop_front();operation_started_us_=Now();
            auto& record=requests_[command.id%requests_.size()];record.status=HV_REQUEST_RUNNING;
            switch(command.operation) {
            case Operation::Start:case Operation::Resume:state_=HV_CONTROL_STARTING;break;
            case Operation::Pause:state_=HV_CONTROL_PAUSING;break;
            case Operation::Reconfigure:case Operation::Source:state_=HV_CONTROL_RECONFIGURING;break;
            default:state_=HV_CONTROL_STOPPING;break;}}
        bool ok=true;std::string error;
        try {
            switch(command.operation) {
            case Operation::Start:case Operation::Resume:
                Retire();ok=Start(error);break;
            case Operation::Source: {
                bool running;
                {std::lock_guard<std::mutex> lock(mutex_);running=session_!=nullptr;source_config_=std::move(command.source);}
                Retire();if(running)ok=Start(error);break;}
            case Operation::Reconfigure: {
                bool running;
                {std::lock_guard<std::mutex> lock(mutex_);running=session_!=nullptr;}
                Retire();{std::lock_guard<std::mutex> lock(mutex_);config_=std::move(command.config);}
                if(running)ok=Start(error);break;}
            case Operation::Pause:case Operation::Stop:case Operation::Close:Retire();break;
            }
        }catch(const std::exception& e){ok=false;error=e.what();}
        catch(...){ok=false;error="Native runtime control operation failed";}
        bool close=false;
        {std::lock_guard<std::mutex> lock(mutex_);
            auto& record=requests_[command.id%requests_.size()];
            if(!ok){state_=HV_CONTROL_ERROR;error_=error;++runtime_errors_;record.status=HV_REQUEST_FAILED;record.result=HV_ERR_INTERNAL;}
            else {error_.clear();record.status=HV_REQUEST_SUCCEEDED;record.result=0;
                switch(command.operation) {
                case Operation::Start:case Operation::Resume:state_=HV_CONTROL_RUNNING;break;
                case Operation::Pause:state_=HV_CONTROL_PAUSED;break;
                case Operation::Close:state_=HV_CONTROL_CLOSED;close=true;break;
                default:state_=session_?HV_CONTROL_RUNNING:HV_CONTROL_STOPPED;break;}}
            if(close_requested_&&command.operation!=Operation::Close&&record.status==HV_REQUEST_FAILED)record.status=HV_REQUEST_CANCELLED;
            const auto deadline=request_deadlines_[command.id%requests_.size()];
            if(deadline&&Now()>deadline) {record.status=HV_REQUEST_TIMED_OUT;record.result=HV_CONTROL_TIMEOUT;}
            record.state=state_;Text(record.message,sizeof(record.message),
                record.status==HV_REQUEST_TIMED_OUT?"Control deadline exceeded; the native owner has now finished this operation":error);operation_started_us_=0;
            bool pending_retirement=false;
            for(const auto& pending:commands_)if(pending.operation!=Operation::Start&&pending.operation!=Operation::Resume)pending_retirement=true;
            if(!pending_retirement)retirement_requested_us_=0;
        }
        if(close){retired_.store(true,std::memory_order_release);return;}
    }
}
int RuntimeController::Copy(int64_t time,HV_CanonicalBodyV1* bodies,uint32_t capacity,uint32_t* written,HV_RuntimeStatsV1* stats) {
    if(written)*written=0;
    if(!written||!stats||stats->struct_size<sizeof(*stats)||stats->api_version!=HV_API_VERSION_040||
       (capacity&&!bodies)||time<0)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);if(!session_||!admission_)return HV_CONTROL_NO_DATA;
    const auto snapshot=session_->Copy(time,*stats);if(capacity<snapshot.count)return HV_ERR_INVALID_ARGUMENT;
    std::copy_n(snapshot.bodies.begin(),snapshot.count,bodies);*written=snapshot.count;return 0;
}
int RuntimeController::Submit(const HV_VideoFrame* frame) {
    if(!frame)return HV_ERR_INVALID_ARGUMENT;std::lock_guard<std::mutex> lock(mutex_);
    if(!session_||!admission_)return HV_ERR_NOT_INITIALIZED;
    if(source_)return HV_ERR_INVALID_ARGUMENT; // one ingestion owner
    FrameEnvelope envelope;envelope.config_epoch=epoch_;
    std::string error;const bool ok=session_->Submit(*frame,envelope,error);if(!ok)error_=error;return ok?0:HV_ERR_INVALID_ARGUMENT;
}
int RuntimeController::SetRegions(const HV_Rect* regions,uint32_t count,int64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);if(!session_||!admission_)return HV_ERR_NOT_INITIALIZED;
    std::string error;const bool ok=session_->SetRegions(regions,count,revision,error);if(!ok)error_=error;return ok?0:HV_ERR_INVALID_ARGUMENT;
}
int RuntimeController::RequestStatus(uint64_t id,HV_RuntimeRequestStatusV1* out) {
    if(!out||out->struct_size<sizeof(*out)||out->api_version!=1||!id)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);const auto& record=requests_[id%requests_.size()];
    if(record.request_id!=id)return HV_CONTROL_NO_DATA;*out=record;out->state=VisibleState();
    if(out->state==HV_CONTROL_ERROR_RETIRING&&out->status<HV_REQUEST_SUCCEEDED) {
        out->status=HV_REQUEST_TIMED_OUT;out->result=HV_CONTROL_TIMEOUT;
        Text(out->message,sizeof(out->message),"Native owner has not retired; resources remain owned. If the driver never returns, restart the process.");}
    return 0;
}
void RuntimeController::FillCapabilities(HV_RuntimeCapabilitiesV1& c) const {
    c={};c.struct_size=sizeof(c);c.api_version=1;
    c.capability_level=session_?(source_?HV_CAPABILITY_INDEPENDENT_PIPELINE:HV_CAPABILITY_ASYNC_PROCESSING):0;
    c.uses_gpu_image_service=source_?uint32_t(source_->UsesGpuImageService()):uint32_t(session_&&session_->UsesGpuRoute());
    Text(c.source,sizeof(c.source),session_?(source_?"native-rtsp":"unity-compatible"):"uninitialized");
    Text(c.image_service,sizeof(c.image_service),session_?(source_?(source_->UsesGpuImageService()?"native-vulkan":"native-cpu-rgba"):"unity-compatible"):"uninitialized");
    Text(c.backend,sizeof(c.backend),session_?session_->ActualBackend():"uninitialized");
    Text(c.fallback_reason,sizeof(c.fallback_reason),fallback_.empty()?error_:fallback_);
}
int RuntimeController::Capabilities(HV_RuntimeCapabilitiesV1* out) {
    if(!out||out->struct_size<sizeof(*out)||out->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);FillCapabilities(*out);return 0;
}
int RuntimeController::Stats(HV_RuntimeStatsV3* out) {
    if(!out||out->struct_size<sizeof(*out)||out->api_version!=3)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);*out={};out->struct_size=sizeof(*out);out->api_version=3;
    out->state=VisibleState();out->config_epoch=epoch_;out->runtime_errors=runtime_errors_+(session_?session_->ErrorCount():0);out->journal_overruns=journal_overruns_;
    out->capability_level=session_?(source_?HV_CAPABILITY_INDEPENDENT_PIPELINE:HV_CAPABILITY_ASYNC_PROCESSING):0;
    if(session_)out->runtime=session_->StatsV2();
    if(source_){const auto s=source_->Progress();out->source_id=source_->Config().source_id;
        out->generation=s.generation;out->frame_id=s.frame_id;out->native_input_frames=s.frames;
        out->input_errors=s.errors;out->clock_offset_us=s.clock.offset_us;out->clock_uncertainty_us=s.clock.uncertainty_us;
        out->source_width=s.source_width;out->source_height=s.source_height;
        out->analysis_width=s.analysis_width;out->analysis_height=s.analysis_height;
        if(out->state==HV_CONTROL_RUNNING){const auto input_state=source_->State();
            if(input_state==HV_INPUT_RECONNECTING)out->state=HV_CONTROL_RECONNECTING;
            else if(input_state==HV_INPUT_FAILED||input_state==HV_INPUT_STOPPED)out->state=HV_CONTROL_ERROR;
            else if(input_state==HV_INPUT_OPENING)out->state=HV_CONTROL_STARTING;}}
    else if(session_)out->frame_id=static_cast<uint64_t>(std::max<int64_t>(0,out->runtime.source_frame_id));
    return 0;
}
int RuntimeController::ReadObservations(uint64_t cursor,HV_RuntimeObservationV1* entries,uint32_t capacity,HV_RuntimeObservationReadV1* result) {
    if(!result||result->struct_size<sizeof(*result)||result->api_version!=1||(capacity&&!entries)||capacity>64)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);*result={};result->struct_size=sizeof(*result);result->api_version=1;result->next_cursor=cursor;
    // One bounded scratch record on stack; callers can batch up to64 without allocations.
    for(uint32_t i=0;i<capacity;++i){ObservationEntry entry;const auto read=journal_->ReadAfter(result->next_cursor,&entry,1);
        result->lost_count+=read.lost_count;if(!read.written)break;
        auto& out=entries[i];out={};out.struct_size=sizeof(out);out.api_version=1;
        out.kind=entry.kind==ObservationKind::Body?0:1;out.body_count=entry.body_count;
        out.sequence=entry.sequence;out.source_id=entry.envelope.source_id;out.generation=entry.envelope.generation;
        out.config_epoch=entry.envelope.config_epoch;out.frame_id=entry.envelope.frame_id;
        out.region_revision=entry.envelope.region_revision;out.body_sequence=entry.body_sequence;out.hand_sequence=entry.hand_sequence;
        out.publication_timestamp_us=entry.published.microseconds;std::copy_n(entry.bodies.begin(),entry.body_count,out.bodies);
        result->next_cursor=read.next_cursor;++result->written;}
    journal_overruns_+=result->lost_count;return 0;
}
int RuntimeController::Diagnostics(char* out,uint32_t capacity) {
    if(!out||!capacity)return HV_ERR_INVALID_ARGUMENT;std::lock_guard<std::mutex> lock(mutex_);
    std::string text=error_;if(session_)text+=(text.empty()?"":"\n")+session_->Diagnostics();
    if(source_){auto error=source_->LastError();if(error.empty()&&source_->State()==HV_INPUT_FAILED) {
            char input_error[1024]{};source_->Api().error(source_->Handle(),input_error,sizeof(input_error));error=input_error;}
        if(!error.empty())text+="\n"+error;text+="\n"+source_->PreviewDiagnostics();}
    if(VisibleState()==HV_CONTROL_ERROR_RETIRING)text+="\nNative owner has not retired; resources remain owned. Restart the process if its driver never returns.";
    Text(out,capacity,text);return 0;
}
int RuntimeController::CopyPreview(uint64_t after,void* bytes,uint32_t capacity,HV_RuntimePreviewV1* info) {
    if(!info||info->struct_size<sizeof(*info)||info->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    return source_&&admission_?source_->CopyPreview(after,bytes,capacity,info):HV_CONTROL_NO_DATA;
}
int RuntimeController::RequestSnapshot(uint64_t* request) {
    if(!request)return HV_ERR_INVALID_ARGUMENT;*request=0;
    std::lock_guard<std::mutex> lock(mutex_);
    return source_&&admission_?source_->RequestSnapshot(request):HV_CONTROL_UNSUPPORTED;
}
int RuntimeController::CopySnapshot(uint64_t request,void* bytes,uint32_t capacity,HV_RuntimePreviewV1* info) {
    if(!request||!info||info->struct_size<sizeof(*info)||info->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    return source_&&admission_?source_->CopySnapshot(request,bytes,capacity,info):HV_CONTROL_NO_DATA;
}
}
