#include "native_vulkan_image_service.h"
#include "input_internal.h"
#include "native_capture_request.h"
#include "native_output_slots.h"
#include "native_live_contract.h"
#include "native_vulkan_context.h"
#include <utility>
namespace hvinput {
struct NativeVulkanImageService::State {
  explicit State(Session &session) : source(session) {}
  Session &source;
  NativeVulkanContext gpu;
  NativeOutputSlots slots,live_slots;
  NativeCaptureRequest capture;
  HV_InputFrameInfo capture_info{};
  HV_InputLiveConfigV1 live_config{};
  uint64_t live_revision=0,analysis_published=0,analysis_claimed=0,live_published=0,live_dropped=0;
  uint32_t source_width=0,source_height=0,crop_width=0,crop_height=0;
  bool live_available=false;
  std::string live_error;
  bool capture_copying = false;
  AndroidDecodedImage pending, active;
  uint64_t generation{}, next_sequence{}, pending_sequence{};
  int rotation{};
  bool mirror{}, closing{}, working{}, done{}, draining{};
  std::thread worker;
  std::mutex mutex;
  std::condition_variable changed;
  void Fill(NativeRgbaSlot &slot,uint64_t sequence,uint32_t index,int degrees,bool mirrored,
            bool top_left=true) {
    AHardwareBuffer_Desc output{}, input{};
    AHardwareBuffer_describe(slot.ahb, &output);
    AHardwareBuffer_describe(active.buffer, &input);
    auto &f = slot.frame;f={};f.size=sizeof(f);f.version=1;
    f.hardware_buffer=slot.ahb;f.hardware_buffer_usage=slot.usage;
    f.source_width=input.width;f.source_height=input.height;f.slot=index;
    f.crop_left=active.crop_left;f.crop_top=active.crop_top;
    f.crop_right=active.crop_right;f.crop_bottom=active.crop_bottom;
    f.rotation_degrees=degrees;f.mirror_x=mirrored;
    f.matrix=active.matrix;f.range=active.color_range;f.transfer=active.transfer;f.primaries=active.primaries;
    auto& info=f.frame;info.size=sizeof(info);info.version=1;
    info.sequence=sequence;info.generation=active.generation;
    info.width=output.width;info.height=output.height;info.stride_bytes=output.width*4;
    info.rgba_bytes=info.stride_bytes*info.height;info.received_timestamp_us=active.received_us;
    info.decoded_timestamp_us=active.decoded_us;info.pts_valid=active.pts_valid;
    info.presentation_timestamp_us=info.pts_valid?active.pts_us:0;
    info.clock_id=ClockId();info.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    info.timestamp_kind=HV_INPUT_TIME_LOCAL_DECODE;info.row_origin=top_left?HV_INPUT_ROW_TOP_LEFT:0;
    info.decode_mode=2;
  }
  void Run() {
    std::unique_lock<std::mutex> lock(mutex);
    for(;;) {
      changed.wait(lock,[this]{return closing||(pending.image&&
        (slots.CanBegin()||(live_config.enabled&&live_error.empty()&&live_slots.CanBegin())));});
      if(closing)break;
      const auto config=live_config;const auto revision=live_revision;
      NativeLiveFit fit{};
      bool live=false;
      if(config.enabled&&live_error.empty()) {
        live=FitNativeLive(uint32_t(pending.crop_right-pending.crop_left),
          uint32_t(pending.crop_bottom-pending.crop_top),config.rotation_degrees,
          config.max_width,config.max_height,fit);
        if(!live) {
          live_error="Original live visible extent exceeds supported 4096 per axis; no reduced preview was published";
          live_available=false;
          live_slots.Invalidate();
        }
      }
      const bool resize=live&&(gpu.LiveWidth()!=fit.width||gpu.LiveHeight()!=fit.height);
      if(resize) {
        live_slots.Invalidate();
        // Consumer leases remain address-stable. A geometry change may skip
        // live work, but it never prevents analysis from taking this image.
        if(!live_slots.Retired())live=false;
      }
      auto reservation=ReserveNativeOutputs(slots,live_slots,live);
      if(config.enabled&&reservation.live<0)++live_dropped;
      if(reservation.analysis<0&&reservation.live<0){pending.Reset();continue;}
      active.TakeFrom(pending);const auto sequence=pending_sequence;
      working=true;
      const bool capture_requested=reservation.analysis>=0&&capture.Begin();
      lock.unlock();
      bool ok=true;bool live_prepared=true;std::string allocation_error;
      int live_index=reservation.live;
      try {
        if(live_index>=0&&resize) {
          live_prepared=gpu.PrepareLive(fit.width,fit.height);
          if(!live_prepared){allocation_error=gpu.error;gpu.error.clear();live_index=-1;}
        }
        if(reservation.analysis>=0||live_index>=0)
          ok=gpu.ConvertBranches(active,reservation.analysis,live_index,rotation,mirror,
            config.rotation_degrees,config.mirror_x!=0,capture_requested);
      }catch(...) {ok=false;gpu.error="exception during native Vulkan conversion";}
      if(!ok)source.SetGpuError("native Vulkan image service",gpu.error.c_str());
      lock.lock();
      if(gpu.unfenced) {
        // Neither ring nor the source AImage may retire without actual proof.
        closing=true;changed.notify_all();return;
      }
      if(!live_prepared){live_error=allocation_error;live_available=false;}
      const bool current=ok&&CanPublishNativeGeneration(active.generation,generation,
        closing,draining,source.stop);
      if(reservation.analysis>=0) {
        if(current) {
          Fill(gpu.slots[reservation.analysis],sequence,reservation.analysis,rotation,mirror);
          if(slots.Publish(reservation.analysis,generation,sequence,true,true))++analysis_published;
          if(capture_requested) {
            if(gpu.CaptureVisible()) {
              capture_info=gpu.slots[reservation.analysis].frame.frame;
              capture_info.width=gpu.CaptureWidth();capture_info.height=gpu.CaptureHeight();
              capture_info.stride_bytes=capture_info.width*4;capture_info.rgba_bytes=capture_info.stride_bytes*capture_info.height;
            }
            capture.Finish(gpu.CaptureVisible());
          }
        } else slots.Discard(reservation.analysis);
      }
      if(reservation.live>=0) {
        if(current&&live_prepared&&live_config.enabled&&revision==live_revision) {
          Fill(gpu.live_slots[reservation.live],sequence,reservation.live,
            config.rotation_degrees,config.mirror_x!=0,false);
          if(live_slots.Publish(reservation.live,generation,sequence,true,true)) {
            ++live_published;live_available=true;
          }
        } else {live_slots.Discard(reservation.live);++live_dropped;}
      }
      active.Reset();working=false;
      if(!ok)closing=true;
      changed.notify_all();
    }
    pending.Reset();slots.Invalidate();live_slots.Invalidate();live_available=false;
    done=true;changed.notify_all();
  }
};
NativeVulkanImageService::NativeVulkanImageService(Session &s)
    : state_(std::make_unique<State>(s)) {}
NativeVulkanImageService::~NativeVulkanImageService() {
  Close();
  if (state_->worker.joinable())
    state_->worker.join();
}
bool NativeVulkanImageService::Open(const HV_InputNativeOptionsV1 &config,
                                    std::string &error) {
  auto &s = *state_;
  if (!s.gpu.Open(config)) {
    error = s.gpu.error;
    return false;
  }
  s.rotation = config.rotation_degrees;
  s.mirror = config.mirror_x != 0;
  s.worker = std::thread(&State::Run, &s);
  return true;
}
void NativeVulkanImageService::Queue(AndroidDecodedImage &image) {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.closing || s.draining || s.source.stop)
    return;
  if (image.generation != s.generation) {
    if (s.generation)
      s.capture.Invalidate();
    s.slots.Invalidate();
    s.live_slots.Invalidate();s.live_available=false;
    s.generation = image.generation;
  }
  AHardwareBuffer_Desc descriptor{};AHardwareBuffer_describe(image.buffer,&descriptor);
  s.source_width=descriptor.width;s.source_height=descriptor.height;
  s.crop_width=uint32_t(image.crop_right-image.crop_left);
  s.crop_height=uint32_t(image.crop_bottom-image.crop_top);
  if(s.pending.image&&s.live_config.enabled)++s.live_dropped;
  s.pending.Reset();
  s.pending.TakeFrom(image);
  s.pending_sequence = ++s.next_sequence;
  s.changed.notify_all();
}
void NativeVulkanImageService::Removed(AImageReader *reader,
                                       AHardwareBuffer *buffer) {
  state_->gpu.registry.Notify(reader, buffer);
}
void NativeVulkanImageService::DrainReader() {
  auto &s = *state_;
  std::unique_lock<std::mutex> lock(s.mutex);
  s.draining = true;
  s.capture.Invalidate();
  s.slots.Invalidate();
  s.live_slots.Invalidate();s.live_available=false;
  s.pending.Reset();
  s.changed.wait(lock, [&] { return !s.working; });
  s.gpu.cache.RemoveAll();
  s.gpu.cache.Collect();
  s.draining = false;
}
void NativeVulkanImageService::Close() {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  s.closing = true;
  s.capture.Invalidate();
  s.slots.Invalidate();
  s.live_slots.Invalidate();s.live_available=false;
  s.pending.Reset();
  s.changed.notify_all();
}
bool NativeVulkanImageService::Retired() {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.done && !s.capture_copying && s.slots.Retired() && s.live_slots.Retired();
}
int NativeVulkanImageService::Device(HV_InputNativeDeviceV1 &d) {
  d = state_->gpu.identity;
  return HV_INPUT_OK;
}
int NativeVulkanImageService::Pool(HV_InputNativePoolV1 &pool) {
  const auto &slots = state_->gpu.slots;
  AHardwareBuffer_Desc desc{};
  AHardwareBuffer_describe(slots[0].ahb, &desc);
  pool = {};
  pool.size = sizeof(pool);
  pool.version = 1;
  pool.width = desc.width;
  pool.height = desc.height;
  pool.format = desc.format;
  pool.slot_count = static_cast<uint32_t>(slots.size());
  for (size_t i = 0; i < slots.size(); ++i) {
    pool.hardware_buffers[i] = slots[i].ahb;
    pool.usage[i] = slots[i].usage;
  }
  return HV_INPUT_OK;
}
int NativeVulkanImageService::Acquire(uint64_t after,
                                      HV_InputNativeFrameV1 &frame,
                                      uint32_t timeout_ms) {
  auto &s = *state_;
  std::unique_lock<std::mutex> lock(s.mutex);
  if (timeout_ms)
    s.changed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
      return s.closing || s.source.stop || s.slots.Available(after);
    });
  if (s.closing || s.source.stop)
    return HV_INPUT_NO_FRAME;
  int i = s.slots.Acquire(after);
  if (i < 0)
    return HV_INPUT_NO_FRAME;
  auto &slot = s.gpu.slots[i];
  frame = slot.frame;
  frame.producer_sync_fd = std::exchange(slot.producer_fd, -1);
  ++s.analysis_claimed;
  return HV_INPUT_OK;
}
int NativeVulkanImageService::Copy(uint32_t i, uint64_t gen, uint64_t seq,
                                   void *dst, uint32_t cap) {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.slots.Leased(i, gen, seq))
    return HV_INPUT_NO_FRAME;
  const auto &slot = s.gpu.slots[i];
  if (!slot.mapped)
    return HV_INPUT_INVALID;
  const auto bytes = slot.frame.frame.rgba_bytes;
  if (cap < bytes)
    return HV_INPUT_BUFFER_TOO_SMALL;
  std::memcpy(dst, slot.mapped, bytes);
  return HV_INPUT_OK;
}
int NativeVulkanImageService::RequestCapture(uint64_t &id) {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (s.closing || s.source.stop || s.draining || s.capture_copying)
    return HV_INPUT_BUSY;
  id = s.capture.Request();
  return id ? HV_INPUT_OK : HV_INPUT_BUSY;
}
int NativeVulkanImageService::CopyCapture(uint64_t id, void *dst, uint32_t cap,
                                          HV_InputFrameInfo &info) {
  auto &s = *state_;
  std::unique_lock<std::mutex> lock(s.mutex);
  if (s.capture_copying)
    return HV_INPUT_BUSY;
  if (!s.capture.Matches(id))
    return HV_INPUT_INVALID;
  if (s.capture.Failed(id)) {
    s.capture.Consume(id);
    return HV_INPUT_ERROR;
  }
  if (!s.capture.Ready(id))
    return HV_INPUT_NO_FRAME;
  info = s.capture_info;
  if (!dst || cap < info.rgba_bytes)
    return HV_INPUT_BUFFER_TOO_SMALL;
  auto *pixels = s.gpu.CaptureBytes();
  s.capture_copying = true;
  lock.unlock();
  std::memcpy(dst, pixels, info.rgba_bytes);
  lock.lock();
  s.capture_copying = false;
  s.capture.Consume(id);
  return HV_INPUT_OK;
}
int NativeVulkanImageService::Release(uint32_t i, uint64_t gen, uint64_t seq) {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.slots.Release(i, gen, seq))
    return HV_INPUT_INVALID;
  s.changed.notify_all();
  return HV_INPUT_OK;
}
int NativeVulkanImageService::ConfigureLive(const HV_InputLiveConfigV1& config,
                                            std::string& error) {
  if(config.size<sizeof(config)||config.version!=1||config.reserved||
     !ValidNativeLiveOptions(config.max_width,config.max_height,config.rotation_degrees,
                             config.mirror_x,config.enabled,config.reserved)) {
    error="Native live requires V1, paired zero original bounds or positive bounds up to 4096, and supported rotation/mirror";
    return HV_INPUT_INVALID;
  }
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  if(s.closing||s.source.stop){error="Native source is closing";return HV_INPUT_NO_FRAME;}
  if(s.live_error.empty()&&s.live_config.size&&
     config.max_width==s.live_config.max_width&&config.max_height==s.live_config.max_height&&
     config.rotation_degrees==s.live_config.rotation_degrees&&config.mirror_x==s.live_config.mirror_x&&
     config.enabled==s.live_config.enabled){error.clear();return HV_INPUT_OK;}
   // First attach only stores configuration/revision. An active analysis job
   // already owns its disabled-live snapshot; the next source image prepares
   // live GPU resources on the producer thread. Existing live reconfiguration
   // must still wait for active conversion and consumer leases to retire.
   if(!CanConfigureNativeLiveMetadata(s.live_config.size!=0,s.working,s.closing,s.draining,s.source.stop)){
     error="Native conversion must retire before live reconfiguration";return HV_INPUT_BUSY;
   }
  s.live_slots.Invalidate();
  if(!s.live_slots.Retired()){error="Native live consumer leases must retire before reconfiguration";return HV_INPUT_BUSY;}
  s.live_config=config;++s.live_revision;s.live_available=false;s.live_error.clear();
  error.clear();s.changed.notify_all();return HV_INPUT_OK;
}
int NativeVulkanImageService::LiveGeometry(HV_InputLiveGeometryV1& geometry) {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  if(!s.live_error.empty())return HV_INPUT_ERROR;
  if(s.closing||s.source.stop||!s.live_config.enabled||!s.generation)return HV_INPUT_NO_FRAME;
  NativeLiveFit fit{};
  if(!FitNativeLive(s.crop_width,s.crop_height,s.live_config.rotation_degrees,
                   s.live_config.max_width,s.live_config.max_height,fit))return HV_INPUT_NO_FRAME;
  geometry={sizeof(geometry),1,s.generation,s.source_width,s.source_height,
    fit.width,fit.height,s.live_config.rotation_degrees,s.live_config.mirror_x};
  return HV_INPUT_OK;
}
int NativeVulkanImageService::AcquireLive(uint64_t after,HV_InputNativeFrameV1& frame) {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  if(s.closing||s.source.stop||!s.live_config.enabled)return HV_INPUT_NO_FRAME;
  int index=s.live_slots.Acquire(after);if(index<0)return HV_INPUT_NO_FRAME;
  frame=s.gpu.live_slots[index].frame;
  // Both branches were published only after WaitForFences completed. The live
  // consumer's separate Unity copy fence still owns this lease until release.
  frame.producer_sync_fd=-1;
  return HV_INPUT_OK;
}
int NativeVulkanImageService::ReleaseLive(uint32_t index,uint64_t generation,uint64_t sequence) {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  if(!s.live_slots.Release(index,generation,sequence))return HV_INPUT_INVALID;
  s.changed.notify_all();return HV_INPUT_OK;
}
void NativeVulkanImageService::FillProgress(HV_InputSourceProgressV1& progress) {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  // Session owns real decoder/acquisition counters and the Input clock/state.
  progress.generation=s.generation;progress.live_published_frames=s.live_published;
  progress.analysis_published_frames=s.analysis_published;progress.analysis_claimed_frames=s.analysis_claimed;
  progress.live_dropped_frames=s.live_dropped;progress.source_width=s.source_width;progress.source_height=s.source_height;
  progress.live_enabled=s.live_config.enabled;
  progress.live_available=s.live_config.enabled&&s.live_available&&!s.closing&&!s.source.stop&&s.live_error.empty();
  NativeLiveFit fit{};
  if(FitNativeLive(s.crop_width,s.crop_height,s.live_config.rotation_degrees,
                   s.live_config.max_width,s.live_config.max_height,fit)) {
    progress.live_width=fit.width;progress.live_height=fit.height;
  }
}
void NativeVulkanImageService::DisableLive() {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
  s.live_config.enabled=0;++s.live_revision;s.live_available=false;
  s.live_slots.Invalidate();s.changed.notify_all();
}
std::string NativeVulkanImageService::LiveError() {
  auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);return s.live_error;
}
} // namespace hvinput
