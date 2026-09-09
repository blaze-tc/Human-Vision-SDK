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
/* Six joints per body: left Hand/Handtip/Thumb, then right. Same sequence/order
 * as GetBodies. capacity counts joints. reserved[0]=1 means derived palm.
 * Legacy 17-point models return invalid hands; the original HV_Body ABI is unchanged. */
HV_API HV_Result HV_CALL HV_GetHandJoints(HV_Handle handle, int64_t expected_sequence,
    HV_Joint* joints, int32_t capacity);
HV_API HV_Result HV_CALL HV_GetStats(HV_Handle handle, HV_Stats* out_stats);
/* Normalized top-left rectangles. count=0 disables regions. Non-overlapping. */
HV_API HV_Result HV_CALL HV_SetRegions(HV_Handle handle, const HV_Rect* regions, int32_t count, int64_t revision);
/* Assignments parallel GetBodies; reject if expected_sequence is no longer current. */
HV_API HV_Result HV_CALL HV_GetRegionAssignments(HV_Handle handle, int64_t expected_sequence,
    int32_t* indices, int32_t capacity, int64_t* revision);
HV_API const char* HV_CALL HV_GetLastError(HV_Handle handle);
HV_API void HV_CALL HV_Destroy(HV_Handle handle);

#ifdef __cplusplus
}
#endif
