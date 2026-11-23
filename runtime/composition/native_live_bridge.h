#pragma once
#include "humanvision/humanvision_runtime_live.h"
#include "humanvision_input_live.h"
#include <memory>
namespace humanvision::runtime {
// The retained module outlives both the source owner and its final live call.
int AttachNativeLiveBridge(const HV_InputLiveApiV1&,HV_InputHandle,
 const HV_RuntimeLiveConfigV1&,uint64_t source,uint64_t epoch,int64_t offset,
 uint64_t clock,std::shared_ptr<void> module,HV_RuntimeLiveBindingV1&);
}
