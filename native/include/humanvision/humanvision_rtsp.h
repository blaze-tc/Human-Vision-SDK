#pragma once
#include "humanvision_c.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional HV_ENABLE_RTSP build. IO/decode/reconnect runs on its own worker.
   Output RGBA rows are top-down, resized within the supplied maximum size.
   timestamp is local steady-clock receive time, not camera capture time. */
HV_API void* HV_CALL HV_RtspOpen(const char* url_utf8, int width, int height, int tcp, int timeout_ms);
/* 1 copied, 0 no newer streaming frame, -1 invalid buffer/arguments. */
HV_API int HV_CALL HV_RtspCopyFrame(void* handle, int64_t after_sequence, void* rgba, int capacity,
    int* width, int* height, int64_t* sequence, int64_t* timestamp_us);
/* 1 connecting, 2 streaming, 3 reconnecting, 4 stopped. */
HV_API int HV_CALL HV_RtspState(void* handle);
/* Handle becomes invalid immediately; releases interrupted worker asynchronously. */
HV_API void HV_CALL HV_RtspClose(void* handle);
#ifdef __cplusplus
}
#endif
