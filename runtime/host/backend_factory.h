#pragma once
#include "host/plugin_registry.h"
namespace humanvision::runtime {
// Immutable ordered candidates. Keep factory alive until pipeline creation ends.
// Returned sessions retain their plugin modules independently of this factory.
class BackendFactory {
public:
 explicit BackendFactory(std::vector<std::shared_ptr<const PluginModule>> candidates):candidates_(std::move(candidates)){}
 HV_HostServicesV1 Services();
private:
 static HV_Result HV_CALL Create(void*,const HV_BackendConfigV1*,const HV_BackendApiV1**,void**,HV_ErrorBufferV1*);
 static void HV_CALL Release(void*,const HV_BackendApiV1*,void*);
 std::vector<std::shared_ptr<const PluginModule>> candidates_;
};
}
