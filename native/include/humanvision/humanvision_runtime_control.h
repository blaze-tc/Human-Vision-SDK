#pragma once
#include "humanvision/humanvision_v2.h"

/* Additive control protocol. None of these handles are HV_RuntimeHandle. */
#define HV_RUNTIME_CONTROL_VERSION 1u
#define HV_RUNTIME_STATS_V3_VERSION 3u
typedef struct HV_RuntimeControllerOpaque* HV_RuntimeControllerHandle;
enum { HV_CONTROL_OK=0, HV_CONTROL_BUSY=1, HV_CONTROL_NO_DATA=2,
       HV_CONTROL_UNSUPPORTED=3, HV_CONTROL_TIMEOUT=4 };
enum { HV_EXECUTION_COMPATIBLE=0, HV_EXECUTION_INDEPENDENT_PREFERRED=1,
       HV_EXECUTION_INDEPENDENT_REQUIRED=2 };
enum { HV_CONTROL_CREATED=0, HV_CONTROL_STARTING=1, HV_CONTROL_RUNNING=2,
       HV_CONTROL_PAUSING=3, HV_CONTROL_PAUSED=4, HV_CONTROL_RECONFIGURING=5,
       HV_CONTROL_RECONNECTING=6, HV_CONTROL_STOPPING=7, HV_CONTROL_STOPPED=8,
       HV_CONTROL_ERROR=9, HV_CONTROL_ERROR_RETIRING=10, HV_CONTROL_CLOSED=11 };
enum { HV_REQUEST_PENDING=0, HV_REQUEST_RUNNING=1, HV_REQUEST_SUCCEEDED=2,
       HV_REQUEST_FAILED=3, HV_REQUEST_CANCELLED=4, HV_REQUEST_TIMED_OUT=5 };
enum { HV_CAPABILITY_ASYNC_PROCESSING=1, HV_CAPABILITY_INDEPENDENT_INPUT=2,
       HV_CAPABILITY_INDEPENDENT_PIPELINE=3 };
enum { HV_NATIVE_SOURCE_NONE=0, HV_NATIVE_SOURCE_RTSP=1 };

typedef struct HV_RuntimeControllerConfigV1 {
    uint32_t struct_size, api_version;
    HV_RuntimeConfigV1 runtime;
    uint32_t execution_policy, retirement_timeout_ms;
} HV_RuntimeControllerConfigV1;
typedef struct HV_RuntimeSourceV1 {
    uint32_t struct_size, api_version, kind, flags;
    const char *library_utf8, *url_utf8, *hardware_avc_codec_utf8;
    uint64_t source_id;
    uint32_t width, height, timeout_ms, reconnect_delay_ms, transport_tcp;
    uint32_t rotation_degrees, mirror_x, reserved;
} HV_RuntimeSourceV1;
typedef struct HV_RuntimeRequestStatusV1 {
    uint32_t struct_size, api_version;
    uint64_t request_id;
    uint32_t status, state;
    int32_t result;
    uint32_t reserved;
    char message[512];
} HV_RuntimeRequestStatusV1;
typedef struct HV_RuntimeCapabilitiesV1 {
    uint32_t struct_size, api_version, capability_level, uses_gpu_image_service;
    char source[128], image_service[128], backend[128], fallback_reason[512];
} HV_RuntimeCapabilitiesV1;
typedef struct HV_RuntimeStatsV3 {
    uint32_t struct_size, api_version, state, capability_level;
    uint64_t config_epoch, source_id, generation, frame_id;
    uint64_t native_input_frames, input_errors, runtime_errors, journal_overruns;
    int64_t clock_offset_us, clock_uncertainty_us;
    HV_RuntimeStatsV2 runtime;
} HV_RuntimeStatsV3;
typedef struct HV_RuntimeObservationV1 {
    uint32_t struct_size, api_version, kind, body_count;
    uint64_t sequence, source_id, generation, config_epoch;
    int64_t frame_id, region_revision, body_sequence, hand_sequence;
    int64_t publication_timestamp_us;
    HV_CanonicalBodyV1 bodies[HV_MAX_PEOPLE];
} HV_RuntimeObservationV1;
typedef struct HV_RuntimeObservationReadV1 {
    uint32_t struct_size, api_version, written, reserved;
    uint64_t next_cursor, lost_count;
} HV_RuntimeObservationReadV1;
/* Preview is copied into caller-owned storage. It never grants a decoder lease.
   NO_DATA/UNSUPPORTED leave ownership with the SDK. Exact snapshot is one pending. */
typedef struct HV_RuntimePreviewV1 {
    uint32_t struct_size, api_version, width, height, stride_bytes, rgba_bytes;
    uint64_t source_id, generation, frame_id, config_epoch;
    int64_t timestamp_us;
} HV_RuntimePreviewV1;

typedef struct HV_RuntimeControlApiV1 {
    uint32_t struct_size, api_version;
    int32_t (HV_CALL *CreateDeferred)(const HV_RuntimeControllerConfigV1*, HV_RuntimeControllerHandle*, char*, uint32_t);
    int32_t (HV_CALL *RequestStart)(HV_RuntimeControllerHandle, uint64_t*);
    int32_t (HV_CALL *RequestSource)(HV_RuntimeControllerHandle, const HV_RuntimeSourceV1*, uint64_t*);
    int32_t (HV_CALL *RequestPause)(HV_RuntimeControllerHandle, uint64_t*);
    int32_t (HV_CALL *RequestResume)(HV_RuntimeControllerHandle, uint64_t*);
    int32_t (HV_CALL *RequestStop)(HV_RuntimeControllerHandle, uint64_t*);
    int32_t (HV_CALL *RequestReconfigure)(HV_RuntimeControllerHandle, const HV_RuntimeControllerConfigV1*, uint64_t*);
    int32_t (HV_CALL *RequestClose)(HV_RuntimeControllerHandle, uint64_t*);
    /* Serialize final TryRelease against every API call; OK invalidates handle. */
    int32_t (HV_CALL *TryRelease)(HV_RuntimeControllerHandle);
    int32_t (HV_CALL *Copy)(HV_RuntimeControllerHandle, int64_t, HV_CanonicalBodyV1*, uint32_t, uint32_t*, HV_RuntimeStatsV1*);
    int32_t (HV_CALL *GetRequestStatus)(HV_RuntimeControllerHandle, uint64_t, HV_RuntimeRequestStatusV1*);
    int32_t (HV_CALL *GetCapabilities)(HV_RuntimeControllerHandle, HV_RuntimeCapabilitiesV1*);
    int32_t (HV_CALL *GetStatsV3)(HV_RuntimeControllerHandle, HV_RuntimeStatsV3*);
    int32_t (HV_CALL *ReadObservations)(HV_RuntimeControllerHandle, uint64_t, HV_RuntimeObservationV1*, uint32_t, HV_RuntimeObservationReadV1*);
    int32_t (HV_CALL *Submit)(HV_RuntimeControllerHandle, const HV_VideoFrame*);
    int32_t (HV_CALL *SetRegions)(HV_RuntimeControllerHandle, const HV_Rect*, uint32_t, int64_t);
    int32_t (HV_CALL *GetDiagnostics)(HV_RuntimeControllerHandle, char*, uint32_t);
    int32_t (HV_CALL *CopyPreview)(HV_RuntimeControllerHandle, uint64_t, void*, uint32_t, HV_RuntimePreviewV1*);
    int32_t (HV_CALL *RequestSnapshot)(HV_RuntimeControllerHandle, uint64_t*);
    int32_t (HV_CALL *CopySnapshot)(HV_RuntimeControllerHandle, uint64_t, void*, uint32_t, HV_RuntimePreviewV1*);
} HV_RuntimeControlApiV1;
#ifdef __cplusplus
extern "C" {
#endif
HV_API int32_t HV_CALL HV_QueryRuntimeControlApiV1(uint32_t version, HV_RuntimeControlApiV1*);
#ifdef __cplusplus
}
#endif
