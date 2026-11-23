#pragma once
#include "humanvision_input_native.h"
/* Additive V1 live-preview consumer ABI. No change to the old Input/NativeV1
   structs or exports. Attach borrows the existing decoder, never opens another
   source and never acquires an analysis publication. Android Vulkan only. */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct HV_InputLiveConsumerOpaque *HV_InputLiveHandle;
#define HV_INPUT_LIVE_RENDER_EVENT_ID 0x48564c
typedef void (HV_INPUT_CALL *HV_InputLiveRenderEvent)(int event_id, void *consumer);
typedef struct HV_InputLiveConfigV1 {
  uint32_t size, version;
  uint32_t max_width, max_height, rotation_degrees, mirror_x, enabled, reserved;
} HV_InputLiveConfigV1;
typedef struct HV_InputLiveGeometryV1 {
  uint32_t size, version;
  uint64_t generation;
  uint32_t source_width, source_height, width, height;
  uint32_t rotation_degrees, mirror_x;
} HV_InputLiveGeometryV1;
typedef struct HV_InputLiveTargetsV1 {
  uint32_t size, version;
  void *textures[3]; /* Initialized Unity RenderTexture native pointers. */
  uint32_t width, height;
  uint64_t target_generation; /* Caller-owned nonzero target lifetime token. */
} HV_InputLiveTargetsV1;
typedef struct HV_InputLiveFrameV1 {
  uint32_t size, version;
  /* Producer live shader writes actual bottom-left rows (row_origin=0).
     AHB RGBA8 contains encoded display RGB, no EOTF. Use RGBA8 sRGB Unity
     targets in Linear projects, UNORM targets in Gamma projects. The bridge
     copies texel codes without a blit/conversion. color_space is sRGB
     only for explicit IEC61966-2-1 source transfer; otherwise unknown, not Linear. */
  HV_InputFrameInfo frame;
  uint64_t target_generation;
  uint32_t slot, source_width, source_height;
  int32_t crop_left, crop_top, crop_right, crop_bottom;
  uint32_t rotation_degrees, mirror_x;
} HV_InputLiveFrameV1;
typedef struct HV_InputSourceProgressV1 {
  uint32_t size, version;
  uint64_t generation, decoded_frames, acquired_frames, live_published_frames;
  uint64_t analysis_published_frames, analysis_claimed_frames, live_dropped_frames;
  int64_t timestamp_us; /* Input monotonic clock, not sensor/Runtime time. */
  uint32_t state, source_width, source_height, live_width, live_height;
  uint32_t live_enabled, live_available, reserved;
} HV_InputSourceProgressV1;
typedef struct HV_InputLiveApiV1 {
  uint32_t size, version;
  /* One consumer per source. Holds source lifetime until TryRelease succeeds. */
  int (HV_INPUT_CALL *Attach)(HV_InputHandle, const HV_InputLiveConfigV1 *, HV_InputLiveHandle *);
  int (HV_INPUT_CALL *Configure)(HV_InputLiveHandle, const HV_InputLiveConfigV1 *);
  int (HV_INPUT_CALL *GetGeometry)(HV_InputLiveHandle, HV_InputLiveGeometryV1 *);
  int (HV_INPUT_CALL *BindTargets)(HV_InputLiveHandle, const HV_InputLiveTargetsV1 *);
  /* Before EACH GL.IssuePluginEventAndData(GetRenderEventFunc(),
     HV_INPUT_LIVE_RENDER_EVENT_ID,handle),
     call PrepareRenderEvent. OK reserves a callback lifetime reference. A
     caller must issue every reserved event; an abandoned event remains BUSY. */
  int (HV_INPUT_CALL *PrepareRenderEvent)(HV_InputLiveHandle);
  HV_InputLiveRenderEvent (HV_INPUT_CALL *GetRenderEventFunc)(void);
  /* Poll leases the newest completed Unity target. Older unobserved outputs
     may be replaced. Release only AFTER actual Unity sampling/copy fences. */
  int (HV_INPUT_CALL *Poll)(HV_InputLiveHandle, uint64_t after_sequence, HV_InputLiveFrameV1 *);
  int (HV_INPUT_CALL *ReleaseSlot)(HV_InputLiveHandle, uint32_t slot, uint64_t target_generation, uint64_t sequence);
  int (HV_INPUT_CALL *RetireTargets)(HV_InputLiveHandle);
  int (HV_INPUT_CALL *TargetsRetired)(HV_InputLiveHandle); /* OK or BUSY. */
  /* Close is nonblocking. Release held targets, keep issuing retirement events,
     then TryRelease until OK. Input Release remains BUSY while attached.
     Serialize final TryRelease against every call on this consumer. OK
     invalidates the handle; never issue another callback or API call after OK. */
  int (HV_INPUT_CALL *Close)(HV_InputLiveHandle);
  int (HV_INPUT_CALL *TryRelease)(HV_InputLiveHandle);
  int (HV_INPUT_CALL *GetProgress)(HV_InputLiveHandle, HV_InputSourceProgressV1 *);
  int (HV_INPUT_CALL *GetLastError)(HV_InputLiveHandle, char *, uint32_t);
} HV_InputLiveApiV1;
HV_INPUT_API int HV_INPUT_CALL HV_Input_QueryLiveApiV1(HV_InputLiveApiV1 *);
/* Android OpenNativeV1 source progress does not require a live consumer, model
   result or claim. Other input paths return INVALID, never invented zero counts. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetSourceProgressV1(HV_InputHandle, HV_InputSourceProgressV1 *);
#ifdef __cplusplus
}
#endif
