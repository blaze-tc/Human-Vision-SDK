#include "humanvision/humanvision_runtime_control.h"
#include <stddef.h>
_Static_assert(sizeof(HV_RuntimeControllerConfigV1)==48,"control config layout");
_Static_assert(sizeof(HV_RuntimeSourceV1)==80,"source layout");
_Static_assert(offsetof(HV_RuntimeControlApiV1,CreateDeferred)==8,"query table layout");
_Static_assert(sizeof(HV_RuntimeControlApiV1)==168,"query table append-only layout");
int main(void) {HV_RuntimeControlApiV1 api={sizeof(api),HV_RUNTIME_CONTROL_VERSION};return api.api_version!=1;}
