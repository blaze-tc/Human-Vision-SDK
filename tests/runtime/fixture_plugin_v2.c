/* C-only ABI fixture. No camera/model execution: used solely to prove that a
 * separately compiled plugin can export the query and survive host ownership. */
#include "humanvision_plugin_v2.h"
#include "humanvision/humanvision_android_gpu.h"
#include <stdlib.h>
#include <string.h>

static HV_Result HV_CALL Create(const HV_GpuBackendConfigV1* config,
    const HV_GpuDeviceContextV1* device, void** out, HV_ErrorBufferV1* error) {
    (void)config; (void)device; (void)error;
    *out = malloc(1);
    return *out ? HV_OK : HV_ERR_INTERNAL;
}
static void HV_CALL Destroy(void* instance) { free(instance); }
static HV_Result HV_CALL Run(void* instance, const HV_GpuFrameRefV1* frame,
    const HV_GpuImageTransformV1* transform, HV_TensorViewV1* outputs,
    uint32_t capacity, uint32_t* count, HV_ErrorBufferV1* error) {
    (void)instance; (void)frame; (void)transform; (void)outputs; (void)capacity; (void)error;
    *count = 0;
    return HV_NO_NEW_RESULT;
}
static HV_Result HV_CALL Info(void* instance, HV_BackendSessionInfoV1* info) {
    (void)instance;
    memcpy(info->actual, "C DLL fixture", sizeof("C DLL fixture"));
    return HV_OK;
}
static const HV_GpuBackendApiV1 backend = {sizeof(backend), 1, Create, Destroy, Run, Info};
HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPluginV2(uint32_t version, HV_PluginApiV2* out) {
    if (version != 2 || !out || out->v1.struct_size < sizeof(*out) || out->v1.api_version != 2)
        return HV_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->v1.struct_size = sizeof(*out);
    out->v1.api_version = 2;
    out->v1.plugin_id = "fixture.c.gpu";
    out->v1.plugin_version = "1.0";
    out->v1.type = HV_PLUGIN_BACKEND;
    out->v1.capabilities = HV_CAP_GPU_INPUT | HV_CAP_TENSOR_INFERENCE;
    out->gpu_backend = &backend;
    return HV_OK;
}
