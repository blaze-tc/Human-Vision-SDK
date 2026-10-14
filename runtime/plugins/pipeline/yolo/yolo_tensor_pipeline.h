#pragma once
#include "humanvision_plugin.h"
extern "C" HV_Result HV_CALL HV_QueryYoloTensorPipelineV1(uint32_t, HV_PluginApiV1*);
// CPU-only identity; the private RKNN entry keeps its original admission gate.
extern "C" HV_Result HV_CALL HV_QueryYoloCpuPipelineV1(uint32_t, HV_PluginApiV1*);
