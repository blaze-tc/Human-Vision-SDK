#pragma once
#include "humanvision_plugin.h"

extern "C" HV_Result HV_CALL HV_QueryRknnPluginV1(uint32_t, HV_PluginApiV1*);

#ifdef __cplusplus
namespace humanvision::runtime::rknn {
struct Diagnostics {
    char runtime_version[256]{}, driver_version[256]{};
    uint32_t core_mask = 0;
    uint64_t completed_runs = 0;
    double init_ms = 0, inputs_set_ms = 0, run_ms = 0, outputs_get_ms = 0, outputs_release_ms = 0;
};
// Private native diagnostics; the caller serializes access with the session worker.
bool GetRknnDiagnostics(void* session, Diagnostics& out) noexcept;
}
#endif
