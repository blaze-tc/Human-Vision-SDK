#pragma once
#include "humanvision_input.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Android ARM64 API26+. For Windows CPU input use HV_Input_Open/CopyRgba. */
typedef struct HV_InputNativeOptionsV1 {
  uint32_t size, version;
  HV_InputOptions source;
  const char
      *hardware_avc_codec; /* platform-selected codec, copied during Open */
  uint32_t width, height, rotation_degrees, mirror_x, enable_readback;
} HV_InputNativeOptionsV1;
typedef struct HV_InputNativeDeviceV1 {
  uint32_t size, version;
  uint8_t device_uuid[16], driver_uuid[16];
  uint32_t api_version, vendor_id, device_id, queue_family;
  uint32_t ycbcr_enabled, ahb_enabled, foreign_enabled, sync_fd_enabled;
} HV_InputNativeDeviceV1;
/* Pool addresses are stable from successful OpenNativeV1 through successful
   HV_Input_Release. For persistent imports, the consumer independently calls
   AHardwareBuffer_acquire/release, and retires its Vulkan objects before
   session release. Pool inspection never grants permission to sample or write
   pixels. */
typedef struct HV_InputNativePoolV1 {
  uint32_t size, version, width, height, format, slot_count;
  void *hardware_buffers[3];
  uint64_t usage[3];
} HV_InputNativePoolV1;
typedef struct HV_InputNativeFrameV1 {
  uint32_t size, version;
  /* pts_valid distinguishes an absent media timestamp from genuine zero (and
     signed media timestamps). received_timestamp_us is the matched compressed
     video packet arrival on frame.clock_id, or -1 when unavailable. */
  HV_InputFrameInfo frame;
  uint32_t slot, source_width, source_height;
  int32_t crop_left, crop_top, crop_right, crop_bottom;
  uint32_t rotation_degrees, mirror_x, matrix, range, transfer, primaries;
  void *hardware_buffer; /* borrowed RGBA8, GENERAL, EXTERNAL_KHR ownership */
  uint64_t hardware_buffer_usage;
  /* Actual export from the submission. Acquire transfers fd to caller (close
     or import exactly once). -1 denotes observed completion. */
  int32_t producer_sync_fd;
} HV_InputNativeFrameV1;
HV_INPUT_API int HV_INPUT_CALL
HV_Input_OpenNativeV1(const HV_InputNativeOptionsV1 *, HV_InputHandle *,
                      char *error, uint32_t error_capacity);
HV_INPUT_API int HV_INPUT_CALL
HV_Input_GetNativeDeviceV1(HV_InputHandle, HV_InputNativeDeviceV1 *);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetNativePoolV1(HV_InputHandle,
                                                        HV_InputNativePoolV1 *);
/* A publication can be acquired once. Hold lease until every external user has
   completed. Serialize final HV_Input_Release against all Input API calls. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_AcquireNativeFrameV1(
    HV_InputHandle, uint64_t after_sequence, HV_InputNativeFrameV1 *);
/* Same acquisition and ownership as Acquire. timeout_ms<=60000; Close wakes a
   waiter immediately. Return NO_FRAME on timeout or cancellation. */
HV_INPUT_API int HV_INPUT_CALL
HV_Input_WaitNativeFrameV1(HV_InputHandle, uint64_t after_sequence,
                           uint32_t timeout_ms, HV_InputNativeFrameV1 *);
HV_INPUT_API int HV_INPUT_CALL
HV_Input_CopyNativeRgbaV1(HV_InputHandle, uint32_t slot, uint64_t generation,
                          uint64_t sequence, void *dst, uint32_t capacity);
/* One outstanding request. The next admitted analysis submission also renders
   the visible decoded crop at full resolution into separate staging. Copy
   returns NO_FRAME while pending, INVALID if invalidated by reconnect/close,
   ERROR on capture failure. With a null/short destination, returns metadata
   and BUFFER_TOO_SMALL and retains the capture. A successful copy consumes it.
 */
HV_INPUT_API int HV_INPUT_CALL
HV_Input_RequestNativeCaptureV1(HV_InputHandle, uint64_t *request);
HV_INPUT_API int HV_INPUT_CALL
HV_Input_CopyNativeCaptureV1(HV_InputHandle, uint64_t request, void *dst,
                             uint32_t capacity, HV_InputFrameInfo *info);
/* Caller has observed actual GPU completion and returned image ownership to
   EXTERNAL_KHR (or never submitted GPU work). No device work may outlive lease.
 */
HV_INPUT_API int HV_INPUT_CALL HV_Input_ReleaseNativeFrameV1(
    HV_InputHandle, uint32_t slot, uint64_t generation, uint64_t sequence);
#ifdef __cplusplus
}
#endif
