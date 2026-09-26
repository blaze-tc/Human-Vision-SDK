#include "host/backend_factory.h"
#include <algorithm>
#include <cstring>
#include <chrono>
namespace humanvision::runtime {
namespace {
void Copy(char* to,size_t capacity,const std::string& from){if(!capacity)return;const auto n=std::min(capacity-1,from.size());std::memcpy(to,from.data(),n);to[n]=0;}
void Error(HV_ErrorBufferV1* out,const std::string& text){if(out&&out->struct_size>=sizeof(*out)&&out->data)Copy(out->data,out->capacity,text);}
struct Lease {
 std::shared_ptr<const PluginModule> module;
 void* instance=nullptr;
 std::string failures;
 std::shared_ptr<BackendDiagnostic> diagnostic;
 ~Lease(){if(instance)try{module->api.backend->destroy(instance);}catch(...){}}
};
HV_Result HV_CALL Run(void* p,const HV_TensorViewV1* in,uint32_t n,HV_TensorViewV1* out,uint32_t capacity,uint32_t* count,HV_ErrorBufferV1* error){
 if(!p)return HV_ERR_INVALID_ARGUMENT;
 auto& lease=*static_cast<Lease*>(p);
 try{
  auto started=std::chrono::steady_clock::now();
  auto result=lease.module->api.backend->run(lease.instance,in,n,out,capacity,count,error);
  if(lease.diagnostic){HV_BackendSessionInfoV1 info{};info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
   if(lease.module->api.backend->session_info(lease.instance,&info)==HV_OK){std::lock_guard<std::mutex> lock(lease.diagnostic->mutex);lease.diagnostic->info=info;lease.diagnostic->inference_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-started).count();}}
  return result;
 }
 catch(...){if(count)*count=0;Error(error,"Backend run threw across C ABI");return HV_ERR_INTERNAL;}
}
HV_Result HV_CALL Info(void* p,HV_BackendSessionInfoV1* out){
 if(!p||!out||out->struct_size<sizeof(*out)||out->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 auto& lease=*static_cast<Lease*>(p);
 try{
  auto result=lease.module->api.backend->session_info(lease.instance,out);
  if(result==HV_OK){out->fallback_reason[sizeof(out->fallback_reason)-1]=0;Copy(out->fallback_reason,sizeof(out->fallback_reason),lease.failures+out->fallback_reason);}
  return result;
 }catch(...){return HV_ERR_INTERNAL;}
}
void HV_CALL Destroy(void* p){delete static_cast<Lease*>(p);}
HV_Result HV_CALL NoCreate(const HV_BackendConfigV1*,void**,HV_ErrorBufferV1*){return HV_ERR_INVALID_ARGUMENT;}
const HV_BackendApiV1 table{sizeof(table),HV_PLUGIN_API_V1,NoCreate,Destroy,Run,Info};
}
HV_HostServicesV1 BackendFactory::Services(){return {sizeof(HV_HostServicesV1),HV_PLUGIN_API_V1,this,Create,Release};}
HV_Result HV_CALL BackendFactory::Create(void* context,const HV_BackendConfigV1* config,const HV_BackendApiV1** api,void** out,HV_ErrorBufferV1* error){
 if(api)*api=nullptr;if(out)*out=nullptr;
 if(!context||!config||!api||!out||config->struct_size<sizeof(*config)||config->api_version!=HV_PLUGIN_API_V1)return HV_ERR_INVALID_ARGUMENT;
 try{
  auto& factory=*static_cast<BackendFactory*>(context);std::string failures;
  for(const auto& module:factory.candidates_){
   if(!module||module->api.type!=HV_PLUGIN_BACKEND)continue;
   auto lease=std::make_unique<Lease>();lease->module=module;
   char text[512]{};HV_ErrorBufferV1 buffer{sizeof(buffer),HV_PLUGIN_API_V1,text,sizeof(text)};
   HV_Result result=HV_ERR_INTERNAL;
   HV_BackendConfigV1 selected=*config;
   if(!factory.allow_fallback_)selected.requested_provider_utf8=module->api.plugin_id;
   try{result=module->api.backend->create(&selected,&lease->instance,&buffer);}catch(...){Copy(text,sizeof(text),"creation threw across C ABI");}
   text[sizeof(text)-1]=0;
   if(result==HV_OK&&lease->instance){
    lease->failures=failures;lease->diagnostic=std::make_shared<BackendDiagnostic>();
    lease->diagnostic->creation_failures=failures;
    lease->diagnostic->model_name=config->model_path_utf8?std::filesystem::u8path(config->model_path_utf8).filename().u8string():"model";
    auto& info=lease->diagnostic->info;info.struct_size=sizeof(info);info.api_version=HV_PLUGIN_API_V1;
    module->api.backend->session_info(lease->instance,&info);
    {std::lock_guard<std::mutex> lock(factory.diagnostics_mutex_);factory.diagnostics_.push_back(lease->diagnostic);}
    *api=&table;*out=lease.release();return HV_OK;
   }
   failures+=std::string(module->api.plugin_id)+": "+(text[0]?text:"creation failed")+"; ";
   if(!factory.allow_fallback_)break;
  }
  Error(error,failures.empty()?"No backend candidates available":failures);return HV_ERR_MODEL_LOAD;
 }catch(...){Error(error,"Backend selection exception");return HV_ERR_INTERNAL;}
}
void HV_CALL BackendFactory::Release(void*,const HV_BackendApiV1* api,void* instance){if(api==&table)Destroy(instance);}
std::string BackendFactory::Diagnostics() const {
 std::lock_guard<std::mutex> lock(diagnostics_mutex_);std::string result;
 for(const auto& diagnostic:diagnostics_){std::lock_guard<std::mutex> item(diagnostic->mutex);auto info=diagnostic->info;
  info.requested[sizeof(info.requested)-1]=0;info.actual[sizeof(info.actual)-1]=0;info.fallback_reason[sizeof(info.fallback_reason)-1]=0;
  if(!result.empty())result+=" | ";result+=diagnostic->model_name+": "+info.requested+" -> "+info.actual+" / "+std::to_string(int(diagnostic->inference_ms))+" ms";
  if(!diagnostic->creation_failures.empty()||info.fallback_reason[0])result+=" ("+diagnostic->creation_failures+info.fallback_reason+")";}
 return result;
}
BackendSelectionDiagnostics BackendFactory::SelectionDiagnostics() const {
 std::lock_guard<std::mutex> lock(diagnostics_mutex_);BackendSelectionDiagnostics result;
 if(diagnostics_.empty())return result;
 std::lock_guard<std::mutex> item(diagnostics_.front()->mutex);auto info=diagnostics_.front()->info;
 info.requested[sizeof(info.requested)-1]=0;info.actual[sizeof(info.actual)-1]=0;
 result.requested=info.requested;result.actual=info.actual;return result;
}
}

namespace humanvision::runtime {
namespace {
struct GpuLease {
    std::shared_ptr<const GpuPluginModule> module;
    void* instance = nullptr;
    ~GpuLease() {
        if (instance) try { module->api.gpu_backend->destroy(instance); } catch (...) {}
    }
};

void HV_CALL DestroyGpuLease(void* instance) { delete static_cast<GpuLease*>(instance); }
HV_Result HV_CALL NoGpuCreate(const HV_GpuBackendConfigV1*, const HV_GpuDeviceContextV1*,
    void** out, HV_ErrorBufferV1*) {
    if (out) *out = nullptr;
    return HV_ERR_INVALID_ARGUMENT;
}
HV_Result HV_CALL RunGpuImage(void* instance, const HV_GpuFrameRefV1* frame,
    const HV_GpuImageTransformV1* transform, HV_TensorViewV1* outputs,
    uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count = 0;
    if (!instance || !frame || !transform || !count || (capacity && !outputs) ||
        frame->struct_size < sizeof(*frame) || frame->api_version != HV_GPU_FRAME_API_V1 ||
        transform->struct_size < sizeof(*transform) || transform->api_version != HV_GPU_FRAME_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLease*>(instance);
    try {
        auto result = lease.module->api.gpu_backend->run_image(lease.instance, frame, transform,
            outputs, capacity, count, error);
        if (result != HV_OK) *count = 0;
        if (*count > capacity) {
            *count = 0;
            Error(error, "GPU backend returned more output views than capacity");
            return HV_ERR_INTERNAL;
        }
        return result;
    } catch (...) {
        *count = 0;
        Error(error, "GPU backend run threw across C ABI");
        return HV_ERR_INTERNAL;
    }
}
HV_Result HV_CALL GpuSessionInfo(void* instance, HV_BackendSessionInfoV1* info) {
    if (!instance || !info || info->struct_size < sizeof(*info) || info->api_version != HV_PLUGIN_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLease*>(instance);
    try { return lease.module->api.gpu_backend->session_info(lease.instance, info); }
    catch (...) { return HV_ERR_INTERNAL; }
}
const HV_GpuBackendApiV1 gpu_lease_api{
    sizeof(HV_GpuBackendApiV1), HV_GPU_FRAME_API_V1, NoGpuCreate, DestroyGpuLease, RunGpuImage, GpuSessionInfo};

struct GpuLeaseV3 {
    std::shared_ptr<const GpuPluginModuleV3> module;
    void* instance = nullptr;
    std::mutex mutex;
    HV_GpuPreparedRefV1 active{};
    uint64_t last_generation = 0;
    uint64_t last_token = 0;
    bool has_active = false;
    bool poisoned = false;
    ~GpuLeaseV3() {
        if (!instance) return;
        if (has_active) try {
            module->api.gpu_backend->prepared->discard_prepared(instance, &active, nullptr);
        } catch (...) {}
        try { module->api.gpu_backend->v1.destroy(instance); } catch (...) {}
    }
};

void HV_CALL DestroyGpuLeaseV3(void* instance) { delete static_cast<GpuLeaseV3*>(instance); }

bool ValidPreparedRef(const HV_GpuPreparedRefV1* ref) {
    return ref && ref->struct_size >= sizeof(*ref) &&
        ref->api_version == HV_GPU_PREPARED_API_V1 && ref->token != 0 &&
        ref->generation != 0 && ref->flags == 0 && ref->reserved == 0;
}

bool MatchesActive(const GpuLeaseV3& lease, const HV_GpuPreparedRefV1* ref) {
    return ValidPreparedRef(ref) && lease.has_active &&
        ref->token == lease.active.token && ref->generation == lease.active.generation &&
        ref->frame_id == lease.active.frame_id && ref->timestamp_us == lease.active.timestamp_us;
}

HV_Result HV_CALL RunGpuImageV3(void* instance, const HV_GpuFrameRefV1* frame,
    const HV_GpuImageTransformV1* transform, HV_TensorViewV1* outputs,
    uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count = 0;
    if (!instance || !frame || !transform || !count || (capacity && !outputs) ||
        frame->struct_size < sizeof(*frame) || frame->api_version != HV_GPU_FRAME_API_V1 ||
        transform->struct_size < sizeof(*transform) || transform->api_version != HV_GPU_FRAME_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLeaseV3*>(instance);
    std::lock_guard<std::mutex> lock(lease.mutex);
    if (lease.poisoned) return HV_ERR_INVALID_ARGUMENT;
    try {
        auto result = lease.module->api.gpu_backend->v1.run_image(lease.instance, frame, transform,
            outputs, capacity, count, error);
        if (result != HV_OK) *count = 0;
        if (*count > capacity) {
            *count = 0; Error(error, "V3 GPU backend returned more output views than capacity");
            return HV_ERR_INTERNAL;
        }
        return result;
    } catch (...) { *count = 0; Error(error, "V3 GPU run threw across C ABI"); return HV_ERR_INTERNAL; }
}
HV_Result HV_CALL GpuSessionInfoV3(void* instance, HV_BackendSessionInfoV1* info) {
    if (!instance || !info || info->struct_size < sizeof(*info) || info->api_version != HV_PLUGIN_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLeaseV3*>(instance);
    std::lock_guard<std::mutex> lock(lease.mutex);
    if (lease.poisoned) return HV_ERR_INVALID_ARGUMENT;
    try { return lease.module->api.gpu_backend->v1.session_info(lease.instance, info); }
    catch (...) { return HV_ERR_INTERNAL; }
}
HV_Result HV_CALL PrepareGpuV3(void* instance, const HV_GpuFrameRefV1* frame,
    const HV_GpuImageTransformV1* transform, HV_GpuPreparedRefV1* out, HV_ErrorBufferV1* error) {
    if (!instance || !frame || !transform || !out ||
        out->struct_size < sizeof(*out) || out->api_version != HV_GPU_PREPARED_API_V1 ||
        frame->struct_size < sizeof(*frame) || frame->api_version != HV_GPU_FRAME_API_V1 ||
        !frame->opaque_slot || frame->generation == 0 || frame->flags != 0 ||
        transform->struct_size < sizeof(*transform) || transform->api_version != HV_GPU_FRAME_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLeaseV3*>(instance);
    std::lock_guard<std::mutex> lock(lease.mutex);
    if (lease.has_active || lease.poisoned) return HV_ERR_INVALID_ARGUMENT;
    *out = {sizeof(*out), HV_GPU_PREPARED_API_V1};
    HV_GpuPreparedRefV1 prepared{sizeof(HV_GpuPreparedRefV1), HV_GPU_PREPARED_API_V1};
    try {
        auto result = lease.module->api.gpu_backend->prepared->prepare_image(
            lease.instance, frame, transform, &prepared, error);
        if (result != HV_OK) {
            if (ValidPreparedRef(&prepared)) {
                if (prepared.generation != frame->generation ||
                    prepared.frame_id != frame->frame_id ||
                    prepared.timestamp_us != frame->timestamp_us ||
                    prepared.generation < lease.last_generation ||
                    (prepared.generation == lease.last_generation &&
                        prepared.token <= lease.last_token)) {
                    lease.poisoned = true;
                } else {
                    lease.last_generation = prepared.generation;
                    lease.last_token = prepared.token;
                }
                HV_Result discard = HV_ERR_INTERNAL;
                try { discard = lease.module->api.gpu_backend->prepared->discard_prepared(
                    lease.instance, &prepared, nullptr); } catch (...) {}
                if (discard != HV_OK) {
                    lease.active = prepared; lease.has_active = true; lease.poisoned = true;
                }
            } else {
                lease.poisoned = true;
            }
            return result;
        }
        if (!ValidPreparedRef(&prepared) || prepared.generation != frame->generation ||
            prepared.frame_id != frame->frame_id || prepared.timestamp_us != frame->timestamp_us ||
            prepared.generation < lease.last_generation ||
            (prepared.generation == lease.last_generation && prepared.token <= lease.last_token)) {
            lease.poisoned = true;
            if (ValidPreparedRef(&prepared)) {
                HV_Result discard = HV_ERR_INTERNAL;
                try { discard = lease.module->api.gpu_backend->prepared->discard_prepared(
                    lease.instance, &prepared, nullptr); } catch (...) {}
                if (discard != HV_OK) { lease.active = prepared; lease.has_active = true; }
            }
            Error(error, "V3 backend returned an invalid or repeated prepared token");
            return HV_ERR_INTERNAL;
        }
        lease.active = prepared; lease.has_active = true;
        lease.last_generation = prepared.generation; lease.last_token = prepared.token;
        *out = prepared;
        return HV_OK;
    } catch (...) {
        lease.poisoned = true;
        if (ValidPreparedRef(&prepared)) {
            HV_Result discard = HV_ERR_INTERNAL;
            try { discard = lease.module->api.gpu_backend->prepared->discard_prepared(
                lease.instance, &prepared, nullptr); } catch (...) {}
            if (discard != HV_OK) { lease.active = prepared; lease.has_active = true; }
        }
        Error(error, "V3 prepare threw across C ABI"); return HV_ERR_INTERNAL;
    }
}
HV_Result HV_CALL RunPreparedGpuV3(void* instance, const HV_GpuPreparedRefV1* ref,
    HV_TensorViewV1* outputs, uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    if (count) *count = 0;
    if (!instance || !count || (capacity && !outputs)) return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLeaseV3*>(instance);
    std::lock_guard<std::mutex> lock(lease.mutex);
    if (lease.poisoned || !MatchesActive(lease, ref)) return HV_ERR_INVALID_ARGUMENT;
    try {
        auto result = lease.module->api.gpu_backend->prepared->run_prepared(
            lease.instance, ref, outputs, capacity, count, error);
        if (result != HV_OK) {
            *count = 0;
            try {
                if (lease.module->api.gpu_backend->prepared->discard_prepared(
                    lease.instance, ref, nullptr) == HV_OK) lease.has_active = false;
            } catch (...) {}
        } else {
            lease.has_active = false;
        }
        if (*count > capacity) {
            *count = 0; Error(error, "V3 prepared backend exceeded output capacity");
            return HV_ERR_INTERNAL;
        }
        return result;
    } catch (...) {
        try {
            if (lease.module->api.gpu_backend->prepared->discard_prepared(
                lease.instance, ref, nullptr) == HV_OK) lease.has_active = false;
        }
        catch (...) {}
        *count = 0;
        Error(error, "V3 prepared run threw across C ABI"); return HV_ERR_INTERNAL;
    }
}
HV_Result HV_CALL DiscardPreparedGpuV3(void* instance, const HV_GpuPreparedRefV1* ref, HV_ErrorBufferV1* error) {
    if (!instance) return HV_ERR_INVALID_ARGUMENT;
    auto& lease = *static_cast<GpuLeaseV3*>(instance);
    std::lock_guard<std::mutex> lock(lease.mutex);
    if (lease.poisoned || !MatchesActive(lease, ref)) return HV_ERR_INVALID_ARGUMENT;
    try {
        auto result = lease.module->api.gpu_backend->prepared->discard_prepared(lease.instance, ref, error);
        if (result == HV_OK) lease.has_active = false;
        return result;
    } catch (...) { Error(error, "V3 prepared discard threw across C ABI"); return HV_ERR_INTERNAL; }
}
const HV_GpuPreparedApiV1 gpu_prepared_lease_api{
    sizeof(HV_GpuPreparedApiV1), HV_GPU_PREPARED_API_V1,
    PrepareGpuV3, RunPreparedGpuV3, DiscardPreparedGpuV3};
const HV_GpuBackendApiV2 gpu_lease_api_v3{
    {sizeof(HV_GpuBackendApiV2), HV_GPU_FRAME_API_V1, NoGpuCreate,
        DestroyGpuLeaseV3, RunGpuImageV3, GpuSessionInfoV3}, &gpu_prepared_lease_api};

bool ValidateGpuPlugin(const HV_PluginApiV2& api, std::string& error) {
    const auto& prefix = api.v1;
    if (prefix.struct_size < sizeof(api) || prefix.api_version != HV_PLUGIN_API_V2 ||
        !prefix.plugin_id || !*prefix.plugin_id || !prefix.plugin_version || !*prefix.plugin_version) {
        error = "GPU plugin metadata size, version or identity is invalid"; return false;
    }
    if ((prefix.type != HV_PLUGIN_BACKEND && prefix.type != HV_PLUGIN_PIPELINE) ||
        (!api.gpu_backend && !api.gpu_pipeline) || !(prefix.capabilities & HV_CAP_GPU_INPUT) ||
        (prefix.type == HV_PLUGIN_BACKEND && !api.gpu_backend) ||
        (prefix.type == HV_PLUGIN_PIPELINE && !api.gpu_pipeline)) {
        error = "GPU plugin type, tables or gpu_input capability is invalid"; return false;
    }
    if (api.gpu_backend) {
        const auto& p = *api.gpu_backend;
        if (p.struct_size < sizeof(p) || p.api_version != HV_GPU_FRAME_API_V1 ||
            !p.create || !p.destroy || !p.run_image || !p.session_info ||
            !(prefix.capabilities & HV_CAP_TENSOR_INFERENCE)) {
            error = "GPU backend callbacks, version or tensor inference capability is invalid"; return false;
        }
    }
    if (api.gpu_pipeline) {
        const auto& p = *api.gpu_pipeline;
        if (p.struct_size < sizeof(p) || p.api_version != HV_GPU_FRAME_API_V1 ||
            !p.create || !p.destroy || !p.process_gpu ||
            prefix.max_people < 1 || prefix.max_people > HV_MAX_PEOPLE ||
            !(prefix.capabilities & (HV_CAP_BODY_POSE | HV_CAP_HAND_POSE))) {
            error = "GPU pipeline callbacks, version, capacity or capabilities are invalid"; return false;
        }
    }
    return true;
}

bool ValidateGpuPluginV3(const HV_PluginApiV3& api, std::string& error) {
    const auto& prefix = api.v1;
    if (prefix.struct_size < sizeof(api) || prefix.api_version != HV_PLUGIN_API_V3 ||
        !prefix.plugin_id || !*prefix.plugin_id || !prefix.plugin_version || !*prefix.plugin_version ||
        (prefix.type != HV_PLUGIN_BACKEND && prefix.type != HV_PLUGIN_PIPELINE) ||
        !(prefix.capabilities & HV_CAP_GPU_INPUT) || (!api.gpu_backend && !api.gpu_pipeline) ||
        (prefix.type == HV_PLUGIN_BACKEND && !api.gpu_backend) ||
        (prefix.type == HV_PLUGIN_PIPELINE && !api.gpu_pipeline)) {
        error = "V3 plugin metadata, type or GPU tables are invalid"; return false;
    }
    if (api.gpu_backend) {
        const auto& b = *api.gpu_backend;
        const auto& v1 = b.v1;
        if (v1.struct_size < sizeof(HV_GpuBackendApiV2) || v1.api_version != HV_GPU_FRAME_API_V1 ||
            !v1.create || !v1.destroy || !v1.run_image || !v1.session_info ||
            !b.prepared || !(prefix.capabilities & HV_CAP_TENSOR_INFERENCE)) {
            error = "V3 backend prefix or prepared table is invalid"; return false;
        }
        const auto& p = *b.prepared;
        if (p.struct_size < sizeof(p) || p.api_version != HV_GPU_PREPARED_API_V1 ||
            !p.prepare_image || !p.run_prepared || !p.discard_prepared) {
            error = "V3 prepared callbacks, size or version are invalid"; return false;
        }
    }
    if (api.gpu_pipeline) {
        const auto& p = *api.gpu_pipeline;
        if (p.struct_size < sizeof(p) || p.api_version != HV_GPU_PIPELINE_API_V2 ||
            !p.create || !p.destroy || !p.process_gpu ||
            prefix.max_people < 1 || prefix.max_people > HV_MAX_PEOPLE ||
            !(prefix.capabilities & (HV_CAP_BODY_POSE | HV_CAP_HAND_POSE))) {
            error = "V3 pipeline callbacks, capacity or capabilities are invalid"; return false;
        }
    }
    return true;
}
}

HV_HostServicesV2 BackendFactory::ServicesV2() {
    return {Services(), CreateGpu, ReleaseGpu};
}
HV_HostServicesV3 BackendFactory::ServicesV3() {
    auto services=HV_HostServicesV3{ServicesV2(), CreateGpuV3, ReleaseGpuV3};
    services.v2.v1.struct_size=sizeof(services);
    return services;
}

bool BackendFactory::RegisterV3(HV_QueryPluginV3Fn query, std::string& error,
    std::shared_ptr<const void> owner) {
    error.clear();
    if (!query) { error = "V3 plugin query callback is missing"; return false; }
    try {
        auto module = std::make_shared<GpuPluginModuleV3>();
        module->owner = std::move(owner);
        module->api.v1.struct_size = sizeof(HV_PluginApiV3);
        module->api.v1.api_version = HV_PLUGIN_API_V3;
        if (query(HV_PLUGIN_API_V3, &module->api) != HV_OK) {
            error = "Plugin rejected V3 API version"; return false;
        }
        if (!ValidateGpuPluginV3(module->api, error)) return false;
        std::lock_guard<std::mutex> lock(gpu_mutex_);
        const std::string id = module->api.v1.plugin_id;
        if (gpu_modules_v3_.count(id)) {
            error = "Duplicate GPU plugin id: " + id; return false;
        }
        gpu_modules_v3_.emplace(id, std::move(module));
        return true;
    } catch (...) { error = "V3 plugin registration raised an exception across C ABI"; return false; }
}

std::shared_ptr<const GpuPluginModuleV3> BackendFactory::FindV3(const std::string& id,
    uint64_t capabilities, std::string& error) const {
    std::lock_guard<std::mutex> lock(gpu_mutex_);
    auto found = gpu_modules_v3_.find(id);
    if (found == gpu_modules_v3_.end()) { error = "V3 plugin is not registered: " + id; return {}; }
    if ((found->second->api.v1.capabilities & capabilities) != capabilities) {
        error = "V3 plugin lacks requested capabilities: " + id; return {};
    }
    error.clear(); return found->second;
}

bool BackendFactory::RegisterV2(HV_QueryPluginV2Fn query, std::string& error,
    std::shared_ptr<const void> owner) {
    error.clear();
    if (!query) { error = "V2 plugin query callback is missing"; return false; }
    try {
        auto module = std::make_shared<GpuPluginModule>();
        module->owner = std::move(owner);
        module->api.v1.struct_size = sizeof(HV_PluginApiV2);
        module->api.v1.api_version = HV_PLUGIN_API_V2;
        if (query(HV_PLUGIN_API_V2, &module->api) != HV_OK) {
            error = "Plugin rejected V2 API version"; return false;
        }
        if (!ValidateGpuPlugin(module->api, error)) return false;
        std::lock_guard<std::mutex> lock(gpu_mutex_);
        const std::string id = module->api.v1.plugin_id;
        if (gpu_modules_.count(id)) { error = "Duplicate V2 plugin id: " + id; return false; }
        gpu_modules_.emplace(id, std::move(module));
        return true;
    } catch (...) { error = "V2 plugin registration raised an exception across C ABI"; return false; }
}

std::shared_ptr<const GpuPluginModule> BackendFactory::FindV2(const std::string& id,
    uint64_t capabilities, std::string& error) const {
    std::lock_guard<std::mutex> lock(gpu_mutex_);
    auto found = gpu_modules_.find(id);
    if (found == gpu_modules_.end()) { error = "V2 plugin is not registered: " + id; return {}; }
    if ((found->second->api.v1.capabilities & capabilities) != capabilities) {
        error = "V2 plugin lacks requested capabilities: " + id; return {};
    }
    error.clear();
    return found->second;
}

HV_Result BackendFactory::CreateGpuBackend(const HV_GpuBackendConfigV1* config,
    const HV_GpuDeviceContextV1* device, const HV_GpuBackendApiV1** api, void** out,
    HV_ErrorBufferV1* error) {
    if (api) *api = nullptr;
    if (out) *out = nullptr;
    if (!config || !device || !api || !out || config->struct_size < sizeof(*config) ||
        config->api_version != HV_GPU_FRAME_API_V1 || device->struct_size < sizeof(*device) ||
        device->api_version != HV_GPU_FRAME_API_V1 || !config->model_manifest_utf8 ||
        !*config->model_manifest_utf8 || !config->asset_root_utf8 || !*config->asset_root_utf8 ||
        !config->requested_provider_utf8 || !*config->requested_provider_utf8) {
        Error(error, "GPU backend requires version-1 config/device and explicit provider, manifest and asset root");
        return HV_ERR_INVALID_ARGUMENT;
    }
    try {
        std::string reason;
        const std::string provider = config->requested_provider_utf8;
        const uint64_t required = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE |
            (provider == "backend.ncnn.vulkan" ?
                HV_CAP_VULKAN | HV_CAP_FP16_STORAGE | HV_CAP_FP16_ARITHMETIC |
                HV_CAP_ANDROID_HARDWARE_BUFFER | HV_CAP_EXTERNAL_SYNC_FD : 0);
        auto module = FindV2(provider, required, reason);
        if (!module || !module->api.gpu_backend) {
            Error(error, module ? "Selected V2 plugin has no GPU backend" : reason);
            return HV_ERR_MODEL_LOAD;
        }
        auto lease = std::make_unique<GpuLease>();
        lease->module = std::move(module);
        char text[512]{};
        HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, text, sizeof(text)};
        HV_Result result;
        try { result = lease->module->api.gpu_backend->create(config, device, &lease->instance, &buffer); }
        catch (...) {
            Error(error, std::string(config->requested_provider_utf8) + ": GPU creation threw across C ABI");
            return HV_ERR_INTERNAL;
        }
        text[sizeof(text)-1] = 0;
        if (result != HV_OK || !lease->instance) {
            Error(error, std::string(config->requested_provider_utf8) + ": " +
                (text[0] ? text : "GPU backend creation failed; no fallback is permitted"));
            return result == HV_OK ? HV_ERR_INTERNAL : result;
        }
        *api = &gpu_lease_api;
        *out = lease.release();
        return HV_OK;
    } catch (...) { Error(error, "GPU backend creation exception; no fallback is permitted"); return HV_ERR_INTERNAL; }
}

HV_Result HV_CALL BackendFactory::CreateGpu(void* context, const HV_GpuBackendConfigV1* config,
    const HV_GpuDeviceContextV1* device, const HV_GpuBackendApiV1** api, void** out, HV_ErrorBufferV1* error) {
    if (!context) {
        if (api) *api = nullptr;
        if (out) *out = nullptr;
        return HV_ERR_INVALID_ARGUMENT;
    }
    return static_cast<BackendFactory*>(context)->CreateGpuBackend(config, device, api, out, error);
}
void HV_CALL BackendFactory::ReleaseGpu(void*, const HV_GpuBackendApiV1* api, void* instance) {
    if (api == &gpu_lease_api) DestroyGpuLease(instance);
}

HV_Result BackendFactory::CreateGpuBackendV3(const HV_GpuBackendConfigV1* config,
    const HV_GpuDeviceContextV1* device, const HV_GpuBackendApiV2** api, void** out,
    HV_ErrorBufferV1* error) {
    if (api) *api = nullptr;
    if (out) *out = nullptr;
    if (!config || !device || !api || !out || config->struct_size < sizeof(*config) ||
        config->api_version != HV_GPU_FRAME_API_V1 || device->struct_size < sizeof(*device) ||
        device->api_version != HV_GPU_FRAME_API_V1 || device->reserved != 0 ||
        !config->model_manifest_utf8 || !*config->model_manifest_utf8 ||
        !config->asset_root_utf8 || !*config->asset_root_utf8 ||
        !config->requested_provider_utf8 || !*config->requested_provider_utf8) {
        Error(error, "V3 GPU backend requires valid config/device and explicit provider, manifest and asset root");
        return HV_ERR_INVALID_ARGUMENT;
    }
    try {
        std::string reason;
        const std::string provider = config->requested_provider_utf8;
        const uint64_t required = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE |
            (provider == "backend.ncnn.vulkan" ?
                HV_CAP_VULKAN | HV_CAP_FP16_STORAGE | HV_CAP_FP16_ARITHMETIC |
                HV_CAP_ANDROID_HARDWARE_BUFFER | HV_CAP_EXTERNAL_SYNC_FD : 0);
        auto module = FindV3(provider, required, reason);
        if (!module || !module->api.gpu_backend) {
            Error(error, module ? "Selected V3 plugin has no GPU backend" : reason);
            return HV_ERR_MODEL_LOAD;
        }
        auto lease = std::make_unique<GpuLeaseV3>();
        lease->module = std::move(module);
        char text[512]{};
        HV_ErrorBufferV1 buffer{sizeof(buffer), HV_PLUGIN_API_V1, text, sizeof(text)};
        HV_Result result;
        try { result = lease->module->api.gpu_backend->v1.create(config, device, &lease->instance, &buffer); }
        catch (...) {
            Error(error, provider + ": V3 GPU creation threw across C ABI");
            return HV_ERR_INTERNAL;
        }
        text[sizeof(text)-1] = 0;
        if (result != HV_OK || !lease->instance) {
            Error(error, provider + ": " +
                (text[0] ? text : "V3 GPU backend creation failed; no fallback is permitted"));
            return result == HV_OK ? HV_ERR_INTERNAL : result;
        }
        *api = &gpu_lease_api_v3;
        *out = lease.release();
        return HV_OK;
    } catch (...) { Error(error, "V3 GPU backend creation exception; no fallback is permitted"); return HV_ERR_INTERNAL; }
}

HV_Result HV_CALL BackendFactory::CreateGpuV3(void* context, const HV_GpuBackendConfigV1* config,
    const HV_GpuDeviceContextV1* device, const HV_GpuBackendApiV2** api, void** out,
    HV_ErrorBufferV1* error) {
    if (!context) {
        if (api) *api = nullptr;
        if (out) *out = nullptr;
        return HV_ERR_INVALID_ARGUMENT;
    }
    return static_cast<BackendFactory*>(context)->CreateGpuBackendV3(config, device, api, out, error);
}
void HV_CALL BackendFactory::ReleaseGpuV3(void*, const HV_GpuBackendApiV2* api, void* instance) {
    if (api == &gpu_lease_api_v3) DestroyGpuLeaseV3(instance);
}
}
