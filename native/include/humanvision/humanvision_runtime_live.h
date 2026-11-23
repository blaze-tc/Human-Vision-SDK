#pragma once
#include "humanvision/humanvision_v2.h"
/* Additive source/live contracts. The old Runtime structs remain unchanged. */
#define HV_RUNTIME_LIVE_RENDER_EVENT_ID 0x48564c
typedef struct HV_RuntimeSourceProgressV1 {
 uint32_t struct_size,api_version;
 uint64_t source_id,generation,config_epoch;
 uint64_t decoded_frames,acquired_frames,live_published_frames;
 uint64_t analysis_published_frames,analysis_claimed_frames,live_dropped_frames;
 int64_t timestamp_us; /* Runtime monotonic snapshot time, not sensor time. */
 uint32_t state,source_width,source_height,live_width,live_height;
 uint32_t live_enabled,live_available,reserved;
} HV_RuntimeSourceProgressV1;
typedef struct HV_RuntimeLiveConfigV1 {
 uint32_t struct_size,api_version,max_width,max_height,rotation_degrees,mirror_x,enabled,reserved;
} HV_RuntimeLiveConfigV1;
typedef struct HV_RuntimeLiveGeometryV1 {
 uint32_t struct_size,api_version;uint64_t generation;
 uint32_t source_width,source_height,width,height,rotation_degrees,mirror_x;
} HV_RuntimeLiveGeometryV1;
typedef struct HV_RuntimeLiveTargetsV1 {
 uint32_t struct_size,api_version;void* textures[3];
 uint32_t width,height;uint64_t target_generation;
} HV_RuntimeLiveTargetsV1;
typedef struct HV_RuntimeLiveImageV1 {
 uint32_t struct_size,api_version;uint64_t sequence,generation;
 uint32_t width,height,stride_bytes,rgba_bytes;
 int64_t received_timestamp_us,decoded_timestamp_us,presentation_timestamp_us;
 uint64_t clock_id;
 uint32_t clock_domain,timestamp_kind,pts_valid,row_origin,color_space,decode_mode;
} HV_RuntimeLiveImageV1;
typedef struct HV_RuntimeLiveFrameV1 {
 uint32_t struct_size,api_version;HV_RuntimeLiveImageV1 frame;
 uint64_t target_generation;uint32_t slot,source_width,source_height;
 int32_t crop_left,crop_top,crop_right,crop_bottom;
 uint32_t rotation_degrees,mirror_x;
} HV_RuntimeLiveFrameV1;
typedef void* HV_RuntimeLiveHandle;
typedef void (HV_CALL *HV_RuntimeLiveRenderEvent)(int,void*);
/* Status values here follow the bounded live consumer: 0 OK,1 no frame,2 busy,
   -1 invalid,-2 buffer too small,-3 error. A handle retains its native module.
   PrepareRenderEvent must precede EVERY issued event; every prepared event
   must be issued with that same handle. TryRelease is serialized against all
   calls and only succeeds after queued callbacks, held slots and GPU fences. */
typedef struct HV_RuntimeLiveApiV1 {
 uint32_t struct_size,api_version;
 int32_t (HV_CALL *Configure)(HV_RuntimeLiveHandle,const HV_RuntimeLiveConfigV1*);
 int32_t (HV_CALL *GetGeometry)(HV_RuntimeLiveHandle,HV_RuntimeLiveGeometryV1*);
 int32_t (HV_CALL *BindTargets)(HV_RuntimeLiveHandle,const HV_RuntimeLiveTargetsV1*);
 int32_t (HV_CALL *PrepareRenderEvent)(HV_RuntimeLiveHandle);
 HV_RuntimeLiveRenderEvent (HV_CALL *GetRenderEventFunc)(void);
 int32_t (HV_CALL *Poll)(HV_RuntimeLiveHandle,uint64_t,HV_RuntimeLiveFrameV1*);
 int32_t (HV_CALL *ReleaseSlot)(HV_RuntimeLiveHandle,uint32_t,uint64_t,uint64_t);
 int32_t (HV_CALL *RetireTargets)(HV_RuntimeLiveHandle);
 int32_t (HV_CALL *TargetsRetired)(HV_RuntimeLiveHandle);
 int32_t (HV_CALL *Close)(HV_RuntimeLiveHandle);
 int32_t (HV_CALL *TryRelease)(HV_RuntimeLiveHandle);
 int32_t (HV_CALL *GetLastError)(HV_RuntimeLiveHandle,char*,uint32_t);
} HV_RuntimeLiveApiV1;
typedef struct HV_RuntimeLiveBindingV1 {
 uint32_t struct_size,api_version;uint64_t source_id,config_epoch;
 int64_t clock_offset_us;uint64_t clock_id;
 HV_RuntimeLiveHandle consumer;const HV_RuntimeLiveApiV1* api;
} HV_RuntimeLiveBindingV1;
