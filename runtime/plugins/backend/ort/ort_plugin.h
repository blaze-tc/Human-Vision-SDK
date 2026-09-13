#pragma once
#include "humanvision_plugin.h"
extern "C" HV_Result HV_CALL HV_QueryOrtCpuPlugin(uint32_t,HV_PluginApiV1*);
extern "C" HV_Result HV_CALL HV_QueryOrtAcceleratedPlugin(uint32_t,HV_PluginApiV1*);
extern "C" HV_Result HV_CALL HV_QueryOrtXnnpackPlugin(uint32_t,HV_PluginApiV1*);
extern "C" HV_Result HV_CALL HV_QueryOrtQnnPlugin(uint32_t,HV_PluginApiV1*);
