#include "humanvision/humanvision_c.h"

extern "C" HV_API const char* HV_CALL HV_GetVersionString(void) {
    return HUMANVISION_VERSION_STRING;
}
