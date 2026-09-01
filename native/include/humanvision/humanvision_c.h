#pragma once

#include "humanvision/humanvision_types.h"

#if defined(_WIN32)
#if defined(HUMANVISION_BUILDING_DLL)
#define HV_API __declspec(dllexport)
#else
#define HV_API __declspec(dllimport)
#endif
#define HV_CALL __cdecl
#else
#define HV_API
#define HV_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

HV_API const char* HV_CALL HV_GetVersionString(void);

HV_API HV_Result HV_CALL HV_Create(const HV_Config* config, HV_Handle* out_handle);
HV_API HV_Result HV_CALL HV_Reconfigure(HV_Handle handle, const HV_Config* config);
HV_API HV_Result HV_CALL HV_SubmitFrame(HV_Handle handle, const HV_VideoFrame* frame);
HV_API HV_Result HV_CALL HV_GetLatestResultMeta(HV_Handle handle, HV_ResultMeta* out_meta);
HV_API int32_t HV_CALL HV_GetBodyCount(HV_Handle handle);
HV_API HV_Result HV_CALL HV_GetBodies(
    HV_Handle handle,
    HV_Body* out_bodies,
    int32_t capacity,
    int32_t* written);
HV_API HV_Result HV_CALL HV_GetStats(HV_Handle handle, HV_Stats* out_stats);
HV_API const char* HV_CALL HV_GetLastError(HV_Handle handle);
HV_API void HV_CALL HV_Destroy(HV_Handle handle);

#ifdef __cplusplus
}
#endif
