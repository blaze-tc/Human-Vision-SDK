#pragma once
#include "humanvision/humanvision_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additive result code; the existing HV_Result enum is unchanged. Unsupported
 * builds return this value, never HV_OK and never select another backend.
 * For a valid runtime, HV_RuntimeGetError reports the failed requirement and
 * rebuild action. Invalid arguments instead return HV_ERR_INVALID_ARGUMENT.
 */
#define HV_ANDROID_GPU_ERR_UNSUPPORTED_PLATFORM ((HV_Result)-6)

/* API version is 1. Texture geometry is the actual oriented source geometry;
 * rotation_degrees is 0/90/180/270; mirrored is 0/1. Metadata is copied into a
 * native-owned event slot. Prepare never waits for GPU work or inference.
 * Event data remains valid until consumed/invalidated; stale generations are
 * safe no-ops. The host owns the slot and synchronization, not the caller.
 * On any failed prepare, out_render_event_data is set to NULL.
 * The callback function is NULL when the bridge is unsupported; never issue an
 * event then. Unsupported status is initialized with COPY_UNAVAILABLE and zero
 * metrics/UUIDs. Too-small or wrong-version output storage remains untouched.
 * All bridge symbols exist on every platform, including builds without Vulkan.
 */
#define HV_ANDROID_GPU_API_V1 1u
#define HV_ANDROID_GPU_UUID_SIZE 16u

typedef enum HV_AndroidGpuCopyPath {
    HV_ANDROID_GPU_COPY_UNAVAILABLE = 0,
    HV_ANDROID_GPU_COPY_BLIT = 1,
    HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT = 2
} HV_AndroidGpuCopyPath;

typedef struct HV_AndroidGpuSubmissionV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* unity_texture;
    int32_t width;
    int32_t height;
    int64_t frame_id;
    int64_t timestamp_us;
    uint32_t rotation_degrees;
    uint32_t mirrored;
} HV_AndroidGpuSubmissionV1;

typedef struct HV_AndroidGpuBridgeStatusV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t copy_path;
    uint32_t ahb_format;
    uint64_t ahb_usage;
    uint64_t ahb_format_features;
    uint64_t submitted_frames;
    uint64_t imported_frames;
    uint64_t dropped_no_slot;
    uint64_t dropped_generation;
    uint8_t unity_device_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t ncnn_device_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t unity_driver_uuid[HV_ANDROID_GPU_UUID_SIZE];
    uint8_t ncnn_driver_uuid[HV_ANDROID_GPU_UUID_SIZE];
} HV_AndroidGpuBridgeStatusV1;

HV_API HV_Result HV_CALL HV_RuntimePrepareAndroidGpuFrame(
    HV_RuntimeHandle runtime,
    const HV_AndroidGpuSubmissionV1* submission,
    void** out_render_event_data);

/* A source lease covers one Unity-owned oriented RenderTexture. The caller
 * must keep its texture and VkImage alive from Begin through End. Call End
 * synchronously before Release/Destroy or source replacement; it closes GPU
 * admission and drains pending native events/views for this generation.
 * Reconfigure and device teardown invalidate the lease. Begin again after a
 * new measured source/generation is configured. */
HV_API HV_Result HV_CALL HV_RuntimeBeginAndroidGpuSourceLease(
    HV_RuntimeHandle runtime, void* unity_texture);
HV_API HV_Result HV_CALL HV_RuntimeEndAndroidGpuSourceLease(
    HV_RuntimeHandle runtime);

HV_API void* HV_CALL HV_GetAndroidGpuRenderEventAndDataFunction(void);

HV_API HV_Result HV_CALL HV_RuntimeGetAndroidGpuBridgeStatus(
    HV_RuntimeHandle runtime,
    HV_AndroidGpuBridgeStatusV1* out_status);

#ifdef __cplusplus
}
#endif
