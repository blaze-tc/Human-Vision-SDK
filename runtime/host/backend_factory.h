#pragma once
#include "host/plugin_registry.h"
#include "humanvision_plugin_v2.h"
#include "humanvision_plugin_v3.h"
namespace humanvision::runtime {
// The owner keeps dynamic query metadata and callback code loaded. Static queries
// may omit it. Leases retain this module after factory destruction.
struct GpuPluginModule {
 HV_PluginApiV2 api{};
 std::shared_ptr<const void> owner;
};
struct GpuPluginModuleV3 {
 HV_PluginApiV3 api{};
 std::shared_ptr<const void> owner;
};
struct BackendDiagnostic {std::mutex mutex;HV_BackendSessionInfoV1 info{};std::string creation_failures,model_name;float inference_ms=0;};
struct BackendSelectionDiagnostics {std::string requested="uninitialized",actual="uninitialized";};
// Immutable ordered candidates. Keep factory alive until pipeline creation ends.
// Returned sessions retain their plugin modules independently of this factory.
class BackendFactory {
public:
 explicit BackendFactory(std::vector<std::shared_ptr<const PluginModule>> candidates,bool allow_fallback=true):candidates_(std::move(candidates)),allow_fallback_(allow_fallback){}
 HV_HostServicesV1 Services();
 HV_HostServicesV2 ServicesV2();
 HV_HostServicesV3 ServicesV3();
 bool RegisterV2(HV_QueryPluginV2Fn query,std::string& error,std::shared_ptr<const void> owner={});
 bool RegisterV3(HV_QueryPluginV3Fn query,std::string& error,std::shared_ptr<const void> owner={});
 std::shared_ptr<const GpuPluginModule> FindV2(const std::string& id,uint64_t capabilities,std::string& error) const;
 std::shared_ptr<const GpuPluginModuleV3> FindV3(const std::string& id,uint64_t capabilities,std::string& error) const;
 HV_Result CreateGpuBackend(const HV_GpuBackendConfigV1*,const HV_GpuDeviceContextV1*,const HV_GpuBackendApiV1**,void**,HV_ErrorBufferV1*);
 HV_Result CreateGpuBackendV3(const HV_GpuBackendConfigV1*,const HV_GpuDeviceContextV1*,const HV_GpuBackendApiV2**,void**,HV_ErrorBufferV1*);
 std::string Diagnostics() const;
 BackendSelectionDiagnostics SelectionDiagnostics() const;
private:
 static HV_Result HV_CALL Create(void*,const HV_BackendConfigV1*,const HV_BackendApiV1**,void**,HV_ErrorBufferV1*);
 static void HV_CALL Release(void*,const HV_BackendApiV1*,void*);
 static HV_Result HV_CALL CreateGpu(void*,const HV_GpuBackendConfigV1*,const HV_GpuDeviceContextV1*,const HV_GpuBackendApiV1**,void**,HV_ErrorBufferV1*);
 static void HV_CALL ReleaseGpu(void*,const HV_GpuBackendApiV1*,void*);
 static HV_Result HV_CALL CreateGpuV3(void*,const HV_GpuBackendConfigV1*,const HV_GpuDeviceContextV1*,const HV_GpuBackendApiV2**,void**,HV_ErrorBufferV1*);
 static void HV_CALL ReleaseGpuV3(void*,const HV_GpuBackendApiV2*,void*);
 mutable std::mutex gpu_mutex_;
 std::map<std::string,std::shared_ptr<const GpuPluginModule>> gpu_modules_;
 std::map<std::string,std::shared_ptr<const GpuPluginModuleV3>> gpu_modules_v3_;
 std::vector<std::shared_ptr<const PluginModule>> candidates_;
 bool allow_fallback_=true;
 mutable std::mutex diagnostics_mutex_;
 std::vector<std::shared_ptr<BackendDiagnostic>> diagnostics_;
};
}
