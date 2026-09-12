#pragma once
#include "host/plugin_registry.h"
namespace humanvision::runtime {
struct BackendDiagnostic {std::mutex mutex;HV_BackendSessionInfoV1 info{};std::string creation_failures,model_name;float inference_ms=0;};
// Immutable ordered candidates. Keep factory alive until pipeline creation ends.
// Returned sessions retain their plugin modules independently of this factory.
class BackendFactory {
public:
 explicit BackendFactory(std::vector<std::shared_ptr<const PluginModule>> candidates):candidates_(std::move(candidates)){}
 HV_HostServicesV1 Services();
 std::string Diagnostics() const;
private:
 static HV_Result HV_CALL Create(void*,const HV_BackendConfigV1*,const HV_BackendApiV1**,void**,HV_ErrorBufferV1*);
 static void HV_CALL Release(void*,const HV_BackendApiV1*,void*);
 std::vector<std::shared_ptr<const PluginModule>> candidates_;
 mutable std::mutex diagnostics_mutex_;
 std::vector<std::shared_ptr<BackendDiagnostic>> diagnostics_;
};
}
