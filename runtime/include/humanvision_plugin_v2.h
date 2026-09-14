#pragma once
#include "humanvision_plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Additive query only: never pass a V1 table to HV_QueryPluginV2.
 * Query caller initializes v1.struct_size=sizeof(HV_PluginApiV2),
 * v1.api_version=2. Successful queries preserve that header and provide immutable
 * metadata/tables until module unload. Either GPU table or both may be present;
 * v1.type is the primary role (PIPELINE or BACKEND), not a new bitmask.
 * V1 CPU tables are optional here. A plugin offering the V1 query must return
 * a separate V1 header and satisfy every original V1 callback requirement.
 * GPU V1 tables/configs use api_version=1. HostServicesV2.v1 retains its original
 * V1 size/version and callbacks; the V2 pipeline create signature establishes
 * the extended host table's presence without reinterpreting V1 services.
 *
 * The host retains the slot lease for the complete observation, including every
 * model invocation and GPU completion. opaque_slot is borrowed only while that
 * lease is held; plugins must not retain it after process_gpu returns. generation
 * identifies the immutable resource generation; stale generations are rejected.
 * No per-frame AHB acquire/release or CPU image mapping is implied by this ABI.
 * run_image borrows frame/transform; output tensors are small model outputs in
 * backend-owned memory valid until the next run/destroy. Caller owns the view
 * array. Backend never decodes skeleton semantics or falls back to a CPU path.
 * channel_order: 1=RGB, 2=BGR; mean/norm apply to the selected output channels.
 * flags and reserved fields must be zero in this version.
 * Callbacks must not throw or transfer allocator-owned C++ objects across C ABI.
 */
#define HV_PLUGIN_API_V2 2u
#define HV_GPU_FRAME_API_V1 1u
#define HV_GPU_UUID_SIZE 16u

typedef enum HV_GpuImageFormatV1 {
    HV_GPU_IMAGE_RGBA8_UNORM = 1,
    HV_GPU_IMAGE_BGRA8_UNORM = 2
} HV_GpuImageFormatV1;

typedef enum HV_GpuTensorTypeV1 {
    HV_GPU_TENSOR_FP32 = 1,
    HV_GPU_TENSOR_FP16 = 2
} HV_GpuTensorTypeV1;

typedef struct HV_GpuFrameRefV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* opaque_slot;
    int32_t width;
    int32_t height;
    int64_t frame_id;
    int64_t timestamp_us;
    uint64_t generation;
    uint32_t image_format;
    uint32_t flags;
} HV_GpuFrameRefV1;

typedef struct HV_GpuImageTransformV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Rect source_rect_px;
    int32_t output_width;
    int32_t output_height;
    uint32_t output_type;
    uint32_t output_elempack;
    uint32_t channel_order;
    float mean[4];
    float norm[4];
} HV_GpuImageTransformV1;

typedef struct HV_GpuDeviceContextV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* host_context;
    uint8_t device_uuid[HV_GPU_UUID_SIZE];
    uint8_t driver_uuid[HV_GPU_UUID_SIZE];
    uint32_t graphics_queue_family;
    uint32_t reserved;
} HV_GpuDeviceContextV1;

typedef struct HV_GpuBackendConfigV1 {
    uint32_t struct_size;
    uint32_t api_version;
    const char* model_manifest_utf8;
    const char* asset_root_utf8;
    const char* requested_provider_utf8;
} HV_GpuBackendConfigV1;

typedef struct HV_GpuBackendApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Result (HV_CALL *create)(const HV_GpuBackendConfigV1* config,
                               const HV_GpuDeviceContextV1* device,
                               void** out_instance,
                               HV_ErrorBufferV1* error);
    void (HV_CALL *destroy)(void* instance);
    HV_Result (HV_CALL *run_image)(void* instance,
                                  const HV_GpuFrameRefV1* frame,
                                  const HV_GpuImageTransformV1* transform,
                                  HV_TensorViewV1* outputs,
                                  uint32_t output_capacity,
                                  uint32_t* out_count,
                                  HV_ErrorBufferV1* error);
    HV_Result (HV_CALL *session_info)(void* instance,
                                     HV_BackendSessionInfoV1* out_info);
} HV_GpuBackendApiV1;

typedef struct HV_HostServicesV2 {
    HV_HostServicesV1 v1;
    HV_Result (HV_CALL *create_gpu_backend)(
        void* context,
        const HV_GpuBackendConfigV1* config,
        const HV_GpuDeviceContextV1* device,
        const HV_GpuBackendApiV1** out_api,
        void** out_instance,
        HV_ErrorBufferV1* error);
    void (HV_CALL *release_gpu_backend)(
        void* context,
        const HV_GpuBackendApiV1* api,
        void* instance);
} HV_HostServicesV2;

typedef struct HV_GpuPipelineApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    HV_Result (HV_CALL *create)(const HV_PipelineConfigV1* config,
                               const HV_HostServicesV2* host,
                               void** out_instance,
                               HV_ErrorBufferV1* error);
    void (HV_CALL *destroy)(void* instance);
    HV_Result (HV_CALL *process_gpu)(void* instance,
                                    const HV_GpuFrameRefV1* frame,
                                    HV_ObservationFrameV1* output,
                                    HV_ErrorBufferV1* error);
} HV_GpuPipelineApiV1;

typedef struct HV_PluginApiV2 {
    HV_PluginApiV1 v1;
    const HV_GpuBackendApiV1* gpu_backend;
    const HV_GpuPipelineApiV1* gpu_pipeline;
} HV_PluginApiV2;

typedef HV_Result (HV_CALL *HV_QueryPluginV2Fn)(
    uint32_t requested_api_version,
    HV_PluginApiV2* out_api);

HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPluginV2(
    uint32_t requested_api_version, HV_PluginApiV2* out_api);

#ifdef __cplusplus
}
#endif
