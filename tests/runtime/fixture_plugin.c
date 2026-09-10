/* Test-only plugin: no model or production recognition behavior. */
#include "humanvision_plugin.h"
static int fixture_context;
static HV_Result HV_CALL create(const HV_PipelineConfigV1* c, const HV_HostServicesV1* s, void** out, HV_ErrorBufferV1* e) {
    (void)c; (void)s; (void)e; *out = &fixture_context; return HV_OK;
}
static void HV_CALL destroy(void* p) { (void)p; }
static HV_Result HV_CALL process(void* p, const HV_PipelineInputV1* in, HV_PipelineOutputV1* out, HV_ErrorBufferV1* e) {
    (void)p; (void)in; (void)e; out->body_count=0;out->hand_count=0;return HV_OK;
}
static const HV_PipelineApiV1 pipeline = {sizeof(HV_PipelineApiV1), HV_PLUGIN_API_V1, create, destroy, process};
HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPlugin(uint32_t requested, HV_PluginApiV1* out) {
    HV_PluginApiV1 value = {sizeof(HV_PluginApiV1), HV_PLUGIN_API_V1, "fixture.dynamic", "1.0.0",
        HV_PLUGIN_PIPELINE, HV_CAP_BODY_POSE, 8, &pipeline, 0};
    if (requested!=HV_PLUGIN_API_V1 || !out || out->struct_size<sizeof(value)) return HV_ERR_INVALID_ARGUMENT;
    *out=value;return HV_OK;
}
