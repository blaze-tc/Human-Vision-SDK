#pragma once
#include "humanvision_plugin.h"

/* Independent optional extension. Existing Plugin ABI1/2/3 tables are unchanged. */
#define HV_IMAGE_SERVICE_API_V1 1u
enum { HV_IMAGE_READY=0, HV_IMAGE_PENDING=1, HV_IMAGE_BUSY=2,
       HV_IMAGE_UNSUPPORTED=3, HV_IMAGE_CANCELLED=4, HV_IMAGE_TIMEOUT=5 };
enum { HV_IMAGE_MEMORY_CPU=1, HV_IMAGE_MEMORY_OPAQUE=2 };
enum { HV_IMAGE_RGBA8=1, HV_IMAGE_BGRA8=2, HV_IMAGE_RGB8=3,
       HV_IMAGE_BGR8=4, HV_IMAGE_RGB32F=5 };
enum { HV_IMAGE_TOP_LEFT=1, HV_IMAGE_BOTTOM_LEFT=2 };
enum { HV_IMAGE_COLOR_RGB=1, HV_IMAGE_COLOR_UNKNOWN=0 };
enum { HV_IMAGE_RANGE_FULL=1, HV_IMAGE_RANGE_UNKNOWN=0 };
enum { HV_IMAGE_IN_FLIGHT=0, HV_IMAGE_COMPLETED=1, HV_IMAGE_COMPLETED_CANCELLED=2 };
enum { HV_IMAGE_NORMALIZE_NONE=0, HV_IMAGE_NORMALIZE_PIPELINE=1, HV_IMAGE_NORMALIZE_SERVICE=2 };
enum { HV_IMAGE_INTERPOLATE_NEAREST=1, HV_IMAGE_INTERPOLATE_BILINEAR=2 };

#ifdef __cplusplus
extern "C" {
#endif
typedef struct HV_ImageIdentityV1 {
 uint64_t source_id, generation, config_epoch;
 int64_t frame_id, timestamp_us, region_revision;
} HV_ImageIdentityV1;
typedef struct HV_ImagePlaneV1 {
 const void* data;
 uint64_t byte_count, offset_bytes;
 uint32_t row_stride_bytes, pixel_stride_bytes;
} HV_ImagePlaneV1;
typedef struct HV_ImageDescriptorV1 {
 uint32_t struct_size, api_version;
 HV_ImageIdentityV1 identity;
 uint32_t width, height, pixel_format, plane_count;
 HV_ImagePlaneV1 planes[3];
 uint64_t format_modifier, owner_token;
 uint32_t memory_kind, completion;
 int32_t acquire_sync_fd;
 uint32_t row_origin, color_matrix, color_range, normalization_passes, reserved;
} HV_ImageDescriptorV1;
typedef struct HV_ImageTransformPlanV1 {
 uint32_t struct_size, api_version;
 HV_ImageIdentityV1 identity;
 uint64_t contract_fingerprint, plan_fingerprint;
 HV_Rect source_crop_px;
 uint32_t output_width, output_height, output_format;
 uint32_t rotation_degrees, mirror_x, interpolation;
 /* Total passes across image service + pipeline/backend must equal this value.
    PIPELINE means the service must leave model preprocessing in the pipeline. */
 uint32_t normalization_owner, total_normalization_passes;
 float mean[3], norm[3], pad_rgb[3];
 uint32_t reserved;
} HV_ImageTransformPlanV1;
typedef struct HV_PreparedImageLeaseV1 {
 uint32_t struct_size, api_version;
 uint64_t lease_token, contract_fingerprint, plan_fingerprint;
 HV_ImageDescriptorV1 image;
 float inverse_transform[6]; /* row-major output-to-source pixel-boundary affine */
 uint32_t normalization_passes, completion;
} HV_PreparedImageLeaseV1;
typedef struct HV_ImageServiceApiV1 {
 uint32_t struct_size, api_version;
 void* context;
 int32_t (HV_CALL *QuerySupport)(void*,const HV_ImageDescriptorV1*,const HV_ImageTransformPlanV1*,HV_ErrorBufferV1*);
 /* Source pointers are borrowed only during Submit. The returned prepared image
    belongs to this service until explicit ReleasePrepared, including on cancel. */
 int32_t (HV_CALL *SubmitTransform)(void*,const HV_ImageDescriptorV1*,const HV_ImageTransformPlanV1*,HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
 int32_t (HV_CALL *WaitTransform)(void*,HV_PreparedImageLeaseV1*,uint32_t timeout_ms,HV_ErrorBufferV1*);
 int32_t (HV_CALL *CancelTransform)(void*,HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
 int32_t (HV_CALL *ReleasePrepared)(void*,const HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
} HV_ImageServiceApiV1;
typedef struct HV_PipelineImageExtensionV1 {
 uint32_t struct_size, api_version;
 const char* plugin_id;
 HV_Result (HV_CALL *DescribeTransform)(void*,const HV_ImageDescriptorV1*,HV_ImageTransformPlanV1*,HV_ErrorBufferV1*);
 /* Same opaque instance/worker as the legacy process function. Original input
    carries unchanged ROI metadata; prepared ownership stays with the service. */
 HV_Result (HV_CALL *ProcessPrepared)(void*,const HV_PipelineInputV1*,const HV_ImageTransformPlanV1*,
                                     const HV_PreparedImageLeaseV1*,HV_PipelineOutputV1*,HV_ErrorBufferV1*);
} HV_PipelineImageExtensionV1;
typedef HV_Result (HV_CALL *HV_QueryPipelineImageExtensionV1Fn)(uint32_t,HV_PipelineImageExtensionV1*);
/* Optional symbol exported by a dynamic pipeline module; static plugins use
   unique names registered explicitly by composition. Query does no model I/O. */
HV_PLUGIN_EXPORT HV_Result HV_CALL HV_QueryPipelineImageExtensionV1(uint32_t,HV_PipelineImageExtensionV1*);
#ifdef __cplusplus
}
#endif
