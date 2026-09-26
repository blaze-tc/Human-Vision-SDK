#pragma once
#include "humanvision_plugin_v3.h"
#include "models/rtmdet/rtmdet_model.h"

namespace humanvision::runtime {
bool BuildGpuPoseCrop(const Detection& box, HV_GpuImageTransformV1& transform,
                      HV_Rect& inverse_rect);
}

extern "C" HV_Result HV_CALL HV_QueryTopDownGpuPipelineV3(
    uint32_t requested_api_version, HV_PluginApiV3* out_api);
