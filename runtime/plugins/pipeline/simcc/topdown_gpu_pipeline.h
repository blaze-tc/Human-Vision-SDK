#pragma once
#include "humanvision_plugin_v3.h"
#include "models/rtmdet/rtmdet_model.h"

namespace humanvision::runtime {
struct DetectorLetterbox { float scale=0, pad_x=0, pad_y=0; };
bool BuildGpuDetectorLetterbox(int source_width, int source_height,
                               int model_width, int model_height,
                               HV_GpuImageTransformV1& transform,
                               DetectorLetterbox& inverse);
bool BuildGpuPoseCrop(const Detection& box, HV_GpuImageTransformV1& transform,
                      HV_Rect& inverse_rect);
}

extern "C" HV_Result HV_CALL HV_QueryTopDownGpuPipelineV3(
    uint32_t requested_api_version, HV_PluginApiV3* out_api);
