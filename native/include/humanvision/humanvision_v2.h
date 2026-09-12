#pragma once
#include "humanvision/humanvision_c.h"

#define HV_API_VERSION_040 0x00040000u
#define HV_CANONICAL_JOINT_COUNT 32
#define HV_MAX_PEOPLE 8

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HV_CanonicalJointId {
    HV_CANONICAL_PELVIS = 0, HV_CANONICAL_SPINE_NAVEL = 1,
    HV_CANONICAL_SPINE_CHEST = 2, HV_CANONICAL_NECK = 3,
    HV_CANONICAL_CLAVICLE_LEFT = 4, HV_CANONICAL_SHOULDER_LEFT = 5,
    HV_CANONICAL_ELBOW_LEFT = 6, HV_CANONICAL_WRIST_LEFT = 7,
    HV_CANONICAL_HAND_LEFT = 8, HV_CANONICAL_HANDTIP_LEFT = 9,
    HV_CANONICAL_THUMB_LEFT = 10, HV_CANONICAL_CLAVICLE_RIGHT = 11,
    HV_CANONICAL_SHOULDER_RIGHT = 12, HV_CANONICAL_ELBOW_RIGHT = 13,
    HV_CANONICAL_WRIST_RIGHT = 14, HV_CANONICAL_HAND_RIGHT = 15,
    HV_CANONICAL_HANDTIP_RIGHT = 16, HV_CANONICAL_THUMB_RIGHT = 17,
    HV_CANONICAL_HIP_LEFT = 18, HV_CANONICAL_KNEE_LEFT = 19,
    HV_CANONICAL_ANKLE_LEFT = 20, HV_CANONICAL_FOOT_LEFT = 21,
    HV_CANONICAL_HIP_RIGHT = 22, HV_CANONICAL_KNEE_RIGHT = 23,
    HV_CANONICAL_ANKLE_RIGHT = 24, HV_CANONICAL_FOOT_RIGHT = 25,
    HV_CANONICAL_HEAD = 26, HV_CANONICAL_NOSE = 27,
    HV_CANONICAL_EYE_LEFT = 28, HV_CANONICAL_EAR_LEFT = 29,
    HV_CANONICAL_EYE_RIGHT = 30, HV_CANONICAL_EAR_RIGHT = 31
} HV_CanonicalJointId;

typedef struct HV_CanonicalJointV1 {
    uint32_t struct_size, api_version;
    float x_px, y_px, x_norm, y_norm, confidence;
    uint8_t valid, derived;
    uint16_t reserved;
    int64_t observation_timestamp_us;
    float prediction_ms;
    uint32_t reserved2;
} HV_CanonicalJointV1;

typedef struct HV_CanonicalBodyV1 {
    uint32_t struct_size, api_version;
    int64_t track_id;
    int32_t region_index, lifecycle;
    int64_t region_revision, source_frame_id, observation_timestamp_us;
    HV_Rect bbox_px;
    float confidence;
    uint32_t reserved;
    HV_CanonicalJointV1 joints[HV_CANONICAL_JOINT_COUNT];
} HV_CanonicalBodyV1;

typedef struct HV_RuntimeConfigV1 {
    uint32_t struct_size, api_version;
    const char* runtime_root_utf8;
    const char* profile_id_utf8;
    int32_t max_people;
    uint32_t reserved;
} HV_RuntimeConfigV1;

typedef struct HV_RuntimeStatsV1 {
    uint32_t struct_size, api_version;
    int64_t body_sequence, hand_sequence, source_frame_id, source_timestamp_us;
    int64_t region_revision, dropped_frames;
    float body_fps, hand_fps, preprocess_ms, inference_ms, postprocess_ms;
    uint32_t reserved;
} HV_RuntimeStatsV1;

typedef void* HV_RuntimeHandle;
/* Native steady clock, also used by RTSP capture; callers can translate clock
 * domains by subtracting native capture age from their own monotonic now. */
HV_API int64_t HV_CALL HV_RuntimeClockUs(void);
/* Additive semantic API. Strings are UTF-8. Submit copies input and returns before
 * inference. Control operations (Create/Destroy) may wait for model loading/jobs.
 * Copy is one coherent snapshot; sample_timestamp_us=0 selects raw observations.
 * Sample time must use the same monotonic clock as input timestamps. */
HV_API HV_Result HV_CALL HV_RuntimeCreate(const HV_RuntimeConfigV1*, HV_RuntimeHandle*, char* error, uint32_t error_capacity);
HV_API HV_Result HV_CALL HV_RuntimeSubmit(HV_RuntimeHandle, const HV_VideoFrame*);
HV_API HV_Result HV_CALL HV_RuntimeSetRegions(HV_RuntimeHandle, const HV_Rect*, uint32_t count, int64_t revision);
HV_API HV_Result HV_CALL HV_RuntimeCopy(HV_RuntimeHandle, int64_t sample_timestamp_us, HV_CanonicalBodyV1*, uint32_t capacity, uint32_t* written, HV_RuntimeStatsV1*);
HV_API HV_Result HV_CALL HV_RuntimeGetError(HV_RuntimeHandle, char* error, uint32_t capacity);
HV_API HV_Result HV_CALL HV_RuntimeGetDiagnostics(HV_RuntimeHandle, char* text, uint32_t capacity);
HV_API void HV_CALL HV_RuntimeDestroy(HV_RuntimeHandle);

#ifdef __cplusplus
}
#endif
