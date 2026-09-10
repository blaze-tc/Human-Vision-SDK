#pragma once
#include "humanvision/humanvision_v2.h"

#define HV_PLUGIN_API_V1 1u
#define HV_CAP_BODY_POSE (1ull << 0)
#define HV_CAP_HAND_POSE (1ull << 1)
#define HV_CAP_MULTI_PERSON (1ull << 2)
#define HV_CAP_TENSOR_INFERENCE (1ull << 3)
#define HV_CAP_DYNAMIC_INPUT (1ull << 4)
#define HV_CAP_BATCH (1ull << 5)
#define HV_CAP_GPU_INPUT (1ull << 6)
#define HV_PLUGIN_PIPELINE 1u
#define HV_PLUGIN_BACKEND 2u
#if defined(_WIN32) && defined(HV_PLUGIN_BUILD_DLL)
#define HV_PLUGIN_EXPORT __declspec(dllexport)
#else
#define HV_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* All pointers are borrowed for the duration of the call unless noted otherwise.
 * Tables/metadata returned by Query remain immutable until the module is unloaded.
 * Create must copy configuration strings it retains. Each instance is called by
 * one worker at a time; distinct instances may run concurrently. No exception or
 * allocator-owned C++ object may cross this ABI. Destroy uses the creator's heap. */
typedef struct HV_ErrorBufferV1 {
    uint32_t struct_size, api_version;
    char* data;
    uint32_t capacity;
} HV_ErrorBufferV1;

typedef struct HV_TensorViewV1 {
    uint32_t struct_size, api_version;
    const char* name;
    uint32_t element_type, rank; /* 1=float32, 2=int64, 3=uint8 */
    int64_t dimensions[8];
    const void* data;
    uint64_t byte_count;
} HV_TensorViewV1;

typedef struct HV_BackendConfigV1 {
    uint32_t struct_size, api_version;
    const char* model_path_utf8;
    const char* options_utf8;
    const char* requested_provider_utf8;
} HV_BackendConfigV1;

typedef struct HV_BackendSessionInfoV1 {
    uint32_t struct_size, api_version;
    char requested[64], actual[64], fallback_reason[512];
    uint32_t accelerated, reserved;
} HV_BackendSessionInfoV1;

typedef struct HV_BackendApiV1 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *create)(const HV_BackendConfigV1*, void**, HV_ErrorBufferV1*);
    void (HV_CALL *destroy)(void*);
    /* Output data/names belong to the instance, valid until next run/destroy.
     * Caller owns the output view array; insufficient capacity is an error. */
    HV_Result (HV_CALL *run)(void*, const HV_TensorViewV1*, uint32_t,
        HV_TensorViewV1*, uint32_t, uint32_t*, HV_ErrorBufferV1*);
    HV_Result (HV_CALL *session_info)(void*, HV_BackendSessionInfoV1*);
} HV_BackendApiV1;

typedef struct HV_HostServicesV1 {
    uint32_t struct_size, api_version;
    void* context;
    HV_Result (HV_CALL *create_backend)(void*, const HV_BackendConfigV1*,
        const HV_BackendApiV1**, void**, HV_ErrorBufferV1*);
    void (HV_CALL *release_backend)(void*, const HV_BackendApiV1*, void*);
} HV_HostServicesV1;

typedef struct HV_PipelineConfigV1 {
    uint32_t struct_size, api_version;
    int32_t max_bodies, reserved;
    const char* model_manifest_utf8;
    const char* asset_root_utf8;
    const char* options_utf8;
} HV_PipelineConfigV1;

typedef struct HV_RegionOfInterestV1 {
    uint32_t struct_size, api_version;
    int64_t request_id; /* Opaque correlation only; plugin does not own identities. */
    HV_Rect bbox_px;
    uint32_t side, reserved;
} HV_RegionOfInterestV1;

typedef struct HV_PipelineInputV1 {
    uint32_t struct_size, api_version;
    HV_VideoFrame frame;
    const HV_RegionOfInterestV1* rois;
    uint32_t roi_count, reserved;
} HV_PipelineInputV1;

typedef struct HV_BodyObservationV1 {
    uint32_t struct_size, api_version;
    HV_Rect bbox_px;
    float confidence;
    uint32_t reserved;
    HV_CanonicalJointV1 joints[HV_CANONICAL_JOINT_COUNT];
} HV_BodyObservationV1;

typedef struct HV_HandObservationV1 {
    uint32_t struct_size, api_version;
    int64_t request_id;
    uint32_t side, reserved;
    HV_CanonicalJointV1 palm, fingertip, thumb;
} HV_HandObservationV1;

typedef struct HV_PipelineOutputV1 {
    uint32_t struct_size, api_version;
    HV_BodyObservationV1* bodies;
    uint32_t body_capacity, body_count;
    HV_HandObservationV1* hands;
    uint32_t hand_capacity, hand_count;
    float preprocess_ms, inference_ms, postprocess_ms;
} HV_PipelineOutputV1;

typedef struct HV_PipelineApiV1 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *create)(const HV_PipelineConfigV1*, const HV_HostServicesV1*, void**, HV_ErrorBufferV1*);
    void (HV_CALL *destroy)(void*);
    HV_Result (HV_CALL *process)(void*, const HV_PipelineInputV1*, HV_PipelineOutputV1*, HV_ErrorBufferV1*);
} HV_PipelineApiV1;

typedef struct HV_PluginApiV1 {
    uint32_t struct_size, api_version;
    const char* plugin_id;
    const char* plugin_version;
    uint32_t type;
    uint64_t capabilities;
    uint32_t max_people;
    const HV_PipelineApiV1* pipeline;
    const HV_BackendApiV1* backend;
} HV_PluginApiV1;

typedef HV_Result (HV_CALL *HV_QueryPluginFn)(uint32_t requested_api, HV_PluginApiV1*);
/* DLL exports this name; statically linked plugins use unique query functions. */
HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPlugin(uint32_t requested_api, HV_PluginApiV1* out_api);

typedef struct HV_ObservationFrameV1 {
    uint32_t struct_size, api_version;
    int64_t sequence, source_frame_id, source_timestamp_us;
    int32_t width, height;
    uint32_t body_count, hand_count;
    HV_BodyObservationV1 bodies[HV_MAX_PEOPLE];
    HV_HandObservationV1 hands[HV_MAX_PEOPLE * 2];
    float preprocess_ms, inference_ms, postprocess_ms;
} HV_ObservationFrameV1;

#ifdef __cplusplus
}
#endif
