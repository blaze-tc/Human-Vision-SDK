#pragma once
#include <stdint.h>
#ifdef _WIN32
#ifdef HV_INPUT_BUILD
#define HV_INPUT_API __declspec(dllexport)
#else
#define HV_INPUT_API __declspec(dllimport)
#endif
#define HV_INPUT_CALL __cdecl
#else
#define HV_INPUT_API __attribute__((visibility("default")))
#define HV_INPUT_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define HV_INPUT_ABI_VERSION 1u
typedef struct HV_InputSessionOpaque *HV_InputHandle;
enum {
  HV_INPUT_OK = 0,
  HV_INPUT_NO_FRAME = 1,
  HV_INPUT_BUSY = 2,
  HV_INPUT_INVALID = -1,
  HV_INPUT_BUFFER_TOO_SMALL = -2,
  HV_INPUT_ERROR = -3
};
enum {
  HV_INPUT_STOPPED = 0,
  HV_INPUT_OPENING = 1,
  HV_INPUT_STREAMING = 2,
  HV_INPUT_RECONNECTING = 3,
  HV_INPUT_FAILED = 4,
  HV_INPUT_CLOSING = 5
};
enum {
  HV_INPUT_CLOCK_NATIVE_MONOTONIC = 1,
  HV_INPUT_TIME_LOCAL_DECODE = 1,
  HV_INPUT_ROW_TOP_LEFT = 1,
  HV_INPUT_COLOR_SRGB = 1,
  HV_INPUT_DECODE_CPU_RGBA = 1
};
typedef struct HV_InputOptions {
  uint32_t size, version;
  const char *url;
  uint32_t max_width, max_height, timeout_ms, reconnect_delay_ms, transport_tcp;
} HV_InputOptions;
typedef struct HV_InputFrameInfo {
  uint32_t size, version;
  uint64_t sequence, generation;
  uint32_t width, height, stride_bytes, rgba_bytes;
  int64_t received_timestamp_us, decoded_timestamp_us,
      presentation_timestamp_us;
  uint64_t clock_id;
  uint32_t clock_domain, timestamp_kind, pts_valid, row_origin, color_space,
      decode_mode;
} HV_InputFrameInfo;
typedef struct HV_InputClockInfo {
  uint32_t size, version;
  uint64_t clock_id;
  int64_t now_us, origin_steady_ticks, steady_ticks_per_second;
  uint32_t clock_domain, reserved;
} HV_InputClockInfo;
/* Independent process/library clock: elapsed us since plugin initialization,
   not UTC, sensor, SDK or managed InputMonotonicClock. Query pairs explicitly
   to map clocks. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_QueryClock(HV_InputClockInfo *info);
HV_INPUT_API int HV_INPUT_CALL HV_Input_Open(const HV_InputOptions *,
                                             HV_InputHandle *);
/* Close only requests cancellation. Poll/GetState/GetLastError remain valid.
   Call Release repeatedly until OK; BUSY never invalidates the handle.
   Caller must serialize Release against all API calls; no calls after
   successful Release. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_Close(HV_InputHandle);
HV_INPUT_API int HV_INPUT_CALL HV_Input_Release(HV_InputHandle);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetState(HV_InputHandle,
                                                 uint32_t *state);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetLastError(HV_InputHandle, char *utf8,
                                                     uint32_t capacity);
HV_INPUT_API int HV_INPUT_CALL HV_Input_PollFrame(HV_InputHandle,
                                                  uint64_t after_sequence,
                                                  HV_InputFrameInfo *);
/* Exact sequence copy avoids metadata/image races. NO_FRAME means
   overwritten/retired. Writes caller-owned top-left RGBA. Metadata and bytes
   are copied atomically. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_CopyRgba(HV_InputHandle,
                                                 uint64_t sequence, void *rgba,
                                                 uint32_t capacity,
                                                 HV_InputFrameInfo *);
#ifdef __cplusplus
}
#endif
