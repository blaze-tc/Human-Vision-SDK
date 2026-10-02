#include "input_internal.h"
#ifdef __ANDROID__
#include "android/input_vulkan_private.h"
#include "android_input_vulkan.h"
#endif
#include <cstring>
#include <memory>
#include <new>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace hvinput {
static const auto origin = std::chrono::steady_clock::now();
int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now() - origin)
      .count();
}
uint64_t ClockId() {
  return static_cast<uint64_t>(origin.time_since_epoch().count()) ^
         (static_cast<uint64_t>(
#ifdef _WIN32
           GetCurrentProcessId()
#else
           getpid()
#endif
          ) << 32);
}
int Session::Interrupt(void *p) {
  auto *s = static_cast<Session *>(p);
  return s->stop.load() || NowUs() >= s->deadline.load();
}
void Session::SetError(const char *stage, int code) {
  char detail[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(code, detail, sizeof(detail));
  std::lock_guard<std::mutex> lock(mutex);
  error = std::string(stage) + ": " + detail;
}
void Session::Run() noexcept {
  while (!stop) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++info.generation;
      latest.clear();
      info.width = info.height = info.stride_bytes = info.rgba_bytes = 0;
    }
    try {
      Decode();
    } catch (...) {
      SetError("RTSP worker failure", AVERROR_UNKNOWN);
    }
    if (stop)
      break;
    state = HV_INPUT_RECONNECTING;
    std::unique_lock<std::mutex> lock(mutex);
    wake.wait_for(lock, std::chrono::milliseconds(reconnect_delay_ms),
                  [this] { return stop.load(); });
  }
  worker_done = true;
}
} // namespace hvinput
namespace {
template <class T> bool Valid(T *p) {
  return p && p->size == sizeof(T) && p->version == HV_INPUT_ABI_VERSION;
}
bool Retired(HV_InputHandle h) {
#ifdef _WIN32
  return h->active_copies == 0 &&
         WaitForSingleObject(h->worker.native_handle(), 0) == WAIT_OBJECT_0;
#else
  return h->active_copies == 0 && h->worker_done
#ifdef __ANDROID__
         && hvinput::InputGpuRetired(h)
#endif
         ;
#endif
}
struct CopyScope {
  hvinput::Session &session;
  explicit CopyScope(hvinput::Session &s) : session(s) {
    ++session.active_copies;
  }
  ~CopyScope() { --session.active_copies; }
};
void FillEmpty(hvinput::Session &s, HV_InputFrameInfo *p) { *p = s.info; }
} // namespace
extern "C" {
HV_INPUT_API int HV_INPUT_CALL HV_Input_QueryClock(HV_InputClockInfo *p) {
  if (!Valid(p))
    return HV_INPUT_INVALID;
  p->clock_id = hvinput::ClockId();
  p->now_us = hvinput::NowUs();
  p->origin_steady_ticks = hvinput::origin.time_since_epoch().count();
  p->steady_ticks_per_second = std::chrono::steady_clock::period::den /
                               std::chrono::steady_clock::period::num;
  p->clock_domain = HV_INPUT_CLOCK_NATIVE_MONOTONIC;
  p->reserved = 0;
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_Open(const HV_InputOptions *o,
                                             HV_InputHandle *out) {
  if (!out)
    return HV_INPUT_INVALID;
  *out = nullptr;
  if (!Valid(o) || !o->url || std::strncmp(o->url, "rtsp://", 7) != 0 ||
      o->max_width < 1 || o->max_height < 1 || o->max_width > 4096 ||
      o->max_height > 4096 || o->timeout_ms < 100 || o->timeout_ms > 60000 ||
      o->reconnect_delay_ms > 60000 || o->transport_tcp > 1)
    return HV_INPUT_INVALID;
  try {
    auto s = std::make_unique<HV_InputSessionOpaque>();
    s->url = o->url;
    s->max_width = o->max_width;
    s->max_height = o->max_height;
    s->timeout_ms = o->timeout_ms;
    s->reconnect_delay_ms = o->reconnect_delay_ms;
    s->tcp = o->transport_tcp != 0;
    s->info.size = sizeof(HV_InputFrameInfo);
    s->info.version = 1;
    s->info.clock_id = hvinput::ClockId();
    s->info.clock_domain = HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    s->info.timestamp_kind = HV_INPUT_TIME_LOCAL_DECODE;
    s->info.row_origin = HV_INPUT_ROW_TOP_LEFT;
    s->info.color_space = HV_INPUT_COLOR_SRGB;
#ifdef _WIN32
    s->info.decode_mode = HV_INPUT_DECODE_CPU_RGBA;
#else
    s->info.decode_mode = 0; // GPU PRIVATE source; no CPU RGBA contract.
#endif
    hvinput::InstallSafeLog();
    #ifdef __ANDROID__
    if (!hvinput::BeginInputGpu(s.get())) return HV_INPUT_BUSY;
#endif
    try { s->worker = std::thread(&hvinput::Session::Run, s.get()); }
    catch (...) {
#ifdef __ANDROID__
      hvinput::DetachInputGpu(s.get());
#endif
      throw;
    }
    *out = s.release();
    return 0;
  } catch (...) {
    return HV_INPUT_ERROR;
  }
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_Close(HV_InputHandle h) {
  if (!h)
    return HV_INPUT_INVALID;
  #ifdef __ANDROID__
  hvinput::CloseInputGpu(h);
#endif
  h->stop = true;
  h->wake.notify_all();
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_Release(HV_InputHandle h) {
  if (!h)
    return HV_INPUT_INVALID;
  if (!h->stop || !Retired(h))
    return HV_INPUT_BUSY;
  std::unique_lock<std::mutex> lock(h->mutex, std::try_to_lock);
  if (!lock.owns_lock())
    return HV_INPUT_BUSY;
  lock.unlock();
  h->worker.join();
#ifdef __ANDROID__
  hvinput::DetachInputGpu(h);
#endif
  delete h;
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetState(HV_InputHandle h,
                                                 uint32_t *out) {
  if (!h || !out)
    return HV_INPUT_INVALID;
  *out = h->stop ? (Retired(h) ? HV_INPUT_STOPPED : HV_INPUT_CLOSING)
                 : h->state.load();
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetLastError(HV_InputHandle h, char *p,
                                                     uint32_t capacity) {
  if (!h || !p || !capacity)
    return HV_INPUT_INVALID;
  std::lock_guard<std::mutex> lock(h->mutex);
  if (capacity <= h->error.size()) {
    p[0] = 0;
    return HV_INPUT_BUFFER_TOO_SMALL;
  }
  std::memcpy(p, h->error.c_str(), h->error.size() + 1);
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_PollFrame(HV_InputHandle h,
                                                  uint64_t after,
                                                  HV_InputFrameInfo *p) {
  if (!h || !Valid(p))
    return HV_INPUT_INVALID;
#ifdef __ANDROID__
  return hvinput::PollInputGpuMetadata(h,after,p);
#else
  std::lock_guard<std::mutex> lock(h->mutex);
  FillEmpty(*h, p);
  if (h->stop || h->state != HV_INPUT_STREAMING || h->latest.empty() ||
      h->info.sequence <= after)
    return HV_INPUT_NO_FRAME;
  return 0;
#endif
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_CopyRgba(HV_InputHandle h,
                                                 uint64_t sequence, void *p,
                                                 uint32_t capacity,
                                                 HV_InputFrameInfo *info) {
  if (!h || !p || !Valid(info))
    return HV_INPUT_INVALID;
  CopyScope copying(*h);
  std::lock_guard<std::mutex> lock(h->mutex);
  if (h->stop || h->state != HV_INPUT_STREAMING || h->latest.empty() ||
      sequence != h->info.sequence)
    return HV_INPUT_NO_FRAME;
  if (capacity < h->latest.size())
    return HV_INPUT_BUFFER_TOO_SMALL;
  std::memcpy(p, h->latest.data(), h->latest.size());
  *info = h->info;
  return 0;
}
}
