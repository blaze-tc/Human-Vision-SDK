#pragma once
#include "humanvision_plugin_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* V3 is a separate query. V1 and V2 queries, structures and callback signatures
 * retain their original byte layouts. A prepared token names one backend-owned,
 * generation-bound small GPU tensor, never a retained AHB slot or CPU image.
 * prepare_image borrows its frame only until the GPU copy has completed. The
 * observation owner retains the AHB for any other GPU roles and retires it only
 * after their completion. A token is consumed by run_prepared or explicitly
 * discarded. Tokens strictly increase within one resource generation; a new
 * generation must be greater than the preceding generation. Plugins and the
 * host reject zero, duplicate, stale or malformed tokens without per-frame
 * token-set allocation.
 * V3 backend v1.struct_size advertises the complete HV_GpuBackendApiV2
 * extent, even though the first five callbacks keep their V1 signatures.
 * Callbacks on one backend instance are serialized by its host lease; different
 * backend instances may execute concurrently. Callbacks must not synchronously
 * reenter that same instance. Destroy requires no active call.
 * Output views remain backend-owned until the next run/discard/destroy.
 */
#define HV_PLUGIN_API_V3 3u
#define HV_GPU_PREPARED_API_V1 1u
#define HV_GPU_PIPELINE_API_V2 2u

/* Optional V3 pipeline input extent. The V1 frame prefix and version remain
 * unchanged so V1 backends can borrow &v1 without learning Region semantics. */
typedef struct HV_GpuFrameRefRegionV1 {
    HV_GpuFrameRefV1 v1;
    int64_t region_revision;
    /* Monotonic native capture time paired at Unity submission; source timestamp remains Unity time. */
    int64_t capture_steady_us;
} HV_GpuFrameRefRegionV1;

/* Optional output extent for the GPU runtime only. The V1 observation prefix,
 * body reserved fields, and public canonical ABI remain unchanged. The sidecar
 * follows each body in its original pipeline output order. */
typedef struct HV_GpuObservationFrameV3 {
    HV_ObservationFrameV1 v1;
    float detector_scores[HV_MAX_PEOPLE];
    int32_t crop_track_ids[HV_MAX_PEOPLE];
} HV_GpuObservationFrameV3;

typedef struct HV_GpuPreparedRefV1 {
    uint32_t struct_size, api_version;
    uint64_t token, generation;
    int64_t frame_id, timestamp_us;
    uint32_t flags, reserved;
} HV_GpuPreparedRefV1;

typedef struct HV_GpuPreparedApiV1 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *prepare_image)(void*, const HV_GpuFrameRefV1*,
        const HV_GpuImageTransformV1*, HV_GpuPreparedRefV1*, HV_ErrorBufferV1*);
    HV_Result (HV_CALL *run_prepared)(void*, const HV_GpuPreparedRefV1*,
        HV_TensorViewV1*, uint32_t, uint32_t*, HV_ErrorBufferV1*);
    HV_Result (HV_CALL *discard_prepared)(void*, const HV_GpuPreparedRefV1*,
        HV_ErrorBufferV1*);
} HV_GpuPreparedApiV1;

typedef struct HV_GpuBackendApiV2 {
    HV_GpuBackendApiV1 v1;
    const HV_GpuPreparedApiV1* prepared;
} HV_GpuBackendApiV2;

typedef struct HV_HostServicesV3 {
    HV_HostServicesV2 v2;
    HV_Result (HV_CALL *create_gpu_backend_v3)(void*,
        const HV_GpuBackendConfigV1*, const HV_GpuDeviceContextV1*,
        const HV_GpuBackendApiV2**, void**, HV_ErrorBufferV1*);
    void (HV_CALL *release_gpu_backend_v3)(void*,
        const HV_GpuBackendApiV2*, void*);
} HV_HostServicesV3;

typedef struct HV_GpuPipelineApiV2 {
    uint32_t struct_size, api_version;
    HV_Result (HV_CALL *create)(const HV_PipelineConfigV1*,
        const HV_HostServicesV3*, void**, HV_ErrorBufferV1*);
    void (HV_CALL *destroy)(void*);
    HV_Result (HV_CALL *process_gpu)(void*, const HV_GpuFrameRefV1*,
        HV_ObservationFrameV1*, HV_ErrorBufferV1*);
} HV_GpuPipelineApiV2;

typedef struct HV_PluginApiV3 {
    HV_PluginApiV1 v1;
    const HV_GpuBackendApiV2* gpu_backend;
    const HV_GpuPipelineApiV2* gpu_pipeline;
} HV_PluginApiV3;

typedef HV_Result (HV_CALL *HV_QueryPluginV3Fn)(
    uint32_t requested_api_version, HV_PluginApiV3* out_api);

HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPluginV3(
    uint32_t requested_api_version, HV_PluginApiV3* out_api);

#ifdef __cplusplus
}
#endif
