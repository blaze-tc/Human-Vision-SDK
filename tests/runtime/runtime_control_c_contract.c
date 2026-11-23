#include "humanvision/humanvision_runtime_control.h"
#include <stddef.h>
_Static_assert(sizeof(HV_RuntimeControllerConfigV1)==48,"control config layout");
_Static_assert(sizeof(HV_RuntimeSourceV1)==80,"source layout");
_Static_assert(offsetof(HV_RuntimeControlApiV1,CreateDeferred)==8,"query table layout");
_Static_assert(offsetof(HV_RuntimeControlApiV1,CancelSnapshot)==168,"twenty existing entries retain their offsets");
_Static_assert(offsetof(HV_RuntimeControlApiV1,GetSourceProgressV1)==176,"twenty-one existing entries retain their offsets");
_Static_assert(sizeof(HV_RuntimeControlApiV1)==192,"query table append-only layout");
_Static_assert(sizeof(HV_RuntimeSourceProgressV1)==120,"source progress additive layout");
_Static_assert(sizeof(HV_RuntimeLiveBindingV1)==56,"retained live binding layout");
_Static_assert(sizeof(HV_RuntimeLiveApiV1)==104,"live API layout");
_Static_assert(sizeof(HV_RuntimeLiveFrameV1)==152,"live frame layout");
int main(void) {HV_RuntimeControlApiV1 api={sizeof(api),HV_RUNTIME_CONTROL_VERSION};return api.api_version!=1;}
