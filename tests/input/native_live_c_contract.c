#include "humanvision_input_live.h"
#include <stddef.h>
_Static_assert(offsetof(HV_InputLiveConfigV1,max_width)==8,"V1 prefix");
_Static_assert(sizeof(HV_InputLiveConfigV1)==32,"V1 config layout");
_Static_assert(offsetof(HV_InputLiveApiV1,Attach)==8,"V1 table prefix");
_Static_assert(sizeof(HV_InputLiveApiV1)==8+14*sizeof(void*),"V1 table layout");
