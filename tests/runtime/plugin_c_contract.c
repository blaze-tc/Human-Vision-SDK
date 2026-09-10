#include "humanvision_plugin.h"
#include <stddef.h>
_Static_assert(offsetof(HV_PluginApiV1, struct_size) == 0, "plugin size prefix");
_Static_assert(offsetof(HV_PluginApiV1, api_version) == 4, "plugin version prefix");
_Static_assert(offsetof(HV_BackendApiV1, struct_size) == 0, "backend prefix");
_Static_assert(offsetof(HV_PipelineApiV1, api_version) == 4, "pipeline prefix");
_Static_assert(HV_CANONICAL_PELVIS == 0 && HV_CANONICAL_EAR_RIGHT == 31, "canonical IDs");
_Static_assert(sizeof(HV_CanonicalJointV1) == 48, "canonical joint layout");
int main(void) { return HV_PLUGIN_API_V1 != 1; }
