#include "input_internal.h"
#include "humanvision_input_native.h"
#include <cstdio>
#ifdef __ANDROID__
#include "android/input_vulkan_private.h"
#include "android/native_vulkan_image_service.h"
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
#ifdef HV_INPUT_ERROR_TESTS
void RunReconnectTestHook(Session &);
#endif
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
  if (gpu_error_recorded)
    return;
  error = std::string(stage) + ": " + detail;
}
void Session::SetGpuError(const char *stage, const char *detail) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!gpu_error_recorded) {
    error = std::string(stage) + ": " + detail;
    gpu_error_recorded = true;
  }
  state = HV_INPUT_FAILED;
  stop = true;
}
bool Session::SetDecoderState(uint32_t next) {
  std::lock_guard<std::mutex> lock(mutex);
  if (gpu_error_recorded || stop)
    return false;
  state = next;
  return true;
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
#ifdef HV_INPUT_ERROR_TESTS
    // Deterministic regression seam; absent from every production target.
    RunReconnectTestHook(*this);
#endif
    if (!SetDecoderState(HV_INPUT_RECONNECTING))
      break;
    std::unique_lock<std::mutex> lock(mutex);
    wake.wait_for(lock, std::chrono::milliseconds(reconnect_delay_ms),
                  [this] { return stop.load(); });
  }
#ifdef __ANDROID__
  if(native_output)native_output->Close();
#endif
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
         && (h->native_output ? h->native_output->Retired() : hvinput::InputGpuRetired(h))
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
static int OpenInput(const HV_InputOptions *o, HV_InputHandle *out,
    const HV_InputNativeOptionsV1 *native, char *error, uint32_t capacity) {
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
    if(native) {
      s->hardware_codec=native->hardware_avc_codec;
      s->native_output=std::make_shared<hvinput::NativeVulkanImageService>(*s);
      std::string detail;
      if(!s->native_output->Open(*native,detail)) {
        std::snprintf(error,capacity,"%s",detail.c_str());
        return HV_INPUT_ERROR;
      }
    } else if (!hvinput::BeginInputGpu(s.get())) return HV_INPUT_BUSY;
#endif
    try { s->worker = std::thread(&hvinput::Session::Run, s.get()); }
    catch (...) {
#ifdef __ANDROID__
      if(!s->native_output)hvinput::DetachInputGpu(s.get());
#endif
      throw;
    }
    *out = s.release();
    return 0;
  } catch (...) {
    return HV_INPUT_ERROR;
  }
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_Open(const HV_InputOptions *o,HV_InputHandle *out) {
  return OpenInput(o,out,nullptr,nullptr,0);
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_OpenNativeV1(const HV_InputNativeOptionsV1* o,
    HV_InputHandle* out,char* error,uint32_t capacity) {
  if(out)*out=nullptr;
  if(!out || !error || !capacity)return HV_INPUT_INVALID;
  error[0]=0;
  if(!Valid(o) || !o->hardware_avc_codec || !o->hardware_avc_codec[0] ||
     !o->width || o->width>4096 || !o->height || o->height>4096 ||
     o->rotation_degrees>270 || o->rotation_degrees%90 || o->mirror_x>1 || o->enable_readback>1) {
    std::snprintf(error,capacity,"invalid native output options, dimensions, transform or hardware AVC codec");
    return HV_INPUT_INVALID;
  }
#ifdef __ANDROID__
  int status=OpenInput(&o->source,out,o,error,capacity);
  if(status!=HV_INPUT_OK && !error[0])std::snprintf(error,capacity,"native Input open failed (%d)",status);
  return status;
#else
  std::snprintf(error,capacity,"native Vulkan output requires Android ARM64");
  return HV_INPUT_ERROR;
#endif
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetNativeDeviceV1(HV_InputHandle h, HV_InputNativeDeviceV1* d) {
#ifdef __ANDROID__
  if(h && h->native_output && Valid(d))return h->native_output->Device(*d);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetNativePoolV1(HV_InputHandle h, HV_InputNativePoolV1* p) {
#ifdef __ANDROID__
  if(h && h->native_output && Valid(p))return h->native_output->Pool(*p);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_WaitNativeFrameV1(HV_InputHandle h,uint64_t after,uint32_t timeout,
    HV_InputNativeFrameV1* f) {
#ifdef __ANDROID__
  if(h && h->native_output && Valid(f) && timeout<=60000)return h->native_output->Acquire(after,*f,timeout);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_AcquireNativeFrameV1(HV_InputHandle h,uint64_t after,HV_InputNativeFrameV1* f) {
#ifdef __ANDROID__
  if(h && h->native_output && Valid(f))return h->native_output->Acquire(after,*f);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_CopyNativeRgbaV1(HV_InputHandle h,uint32_t slot,uint64_t gen,uint64_t seq,
    void* dst,uint32_t cap) {
#ifdef __ANDROID__
  if(h && h->native_output && dst)return h->native_output->Copy(slot,gen,seq,dst,cap);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_RequestNativeCaptureV1(HV_InputHandle h,uint64_t* id) {
#ifdef __ANDROID__
  if(h && h->native_output && id)return h->native_output->RequestCapture(*id);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_CopyNativeCaptureV1(HV_InputHandle h,uint64_t id,void* dst,
    uint32_t cap,HV_InputFrameInfo* info) {
#ifdef __ANDROID__
  if(h && h->native_output && Valid(info))return h->native_output->CopyCapture(id,dst,cap,*info);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_ReleaseNativeFrameV1(HV_InputHandle h,uint32_t slot,uint64_t gen,uint64_t seq) {
#ifdef __ANDROID__
  if(h && h->native_output)return h->native_output->Release(slot,gen,seq);
#endif
  return HV_INPUT_INVALID;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_Close(HV_InputHandle h) {
  if (!h)
    return HV_INPUT_INVALID;
  #ifdef __ANDROID__
  if(h->native_output)h->native_output->Close(); else hvinput::CloseInputGpu(h);
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
  if(!h->native_output)hvinput::DetachInputGpu(h);
#endif
  delete h;
  return 0;
}
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetState(HV_InputHandle h,
                                                 uint32_t *out) {
  if (!h || !out)
    return HV_INPUT_INVALID;
#ifdef __ANDROID__
  if(h->native_output && h->state==HV_INPUT_FAILED){*out=HV_INPUT_FAILED;return 0;}
#endif
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
