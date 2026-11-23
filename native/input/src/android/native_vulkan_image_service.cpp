#include "native_vulkan_image_service.h"
#include "input_internal.h"
#include "native_capture_request.h"
#include "native_output_slots.h"
#include "native_vulkan_context.h"
#include <utility>
namespace hvinput {
struct NativeVulkanImageService::State {
  explicit State(Session &session) : source(session) {}
  Session &source;
  NativeVulkanContext gpu;
  NativeOutputSlots slots;
  NativeCaptureRequest capture;
  HV_InputFrameInfo capture_info{};
  bool capture_copying = false;
  AndroidDecodedImage pending, active;
  uint64_t generation{}, next_sequence{}, pending_sequence{};
  int rotation{};
  bool mirror{}, closing{}, working{}, done{}, draining{};
  std::thread worker;
  std::mutex mutex;
  std::condition_variable changed;
  void Fill(NativeRgbaSlot &slot, uint64_t sequence) {
    AHardwareBuffer_Desc output{}, input{};
    AHardwareBuffer_describe(slot.ahb, &output);
    AHardwareBuffer_describe(active.buffer, &input);
    auto &f = slot.frame;
    f = {};
    f.size = sizeof(f);
    f.version = 1;
    f.hardware_buffer = slot.ahb;
    f.hardware_buffer_usage = slot.usage;
    f.source_width = input.width;
    f.source_height = input.height;
    f.slot = static_cast<uint32_t>(&slot - gpu.slots.data());
    f.crop_left = active.crop_left;
    f.crop_top = active.crop_top;
    f.crop_right = active.crop_right;
    f.crop_bottom = active.crop_bottom;
    f.rotation_degrees = rotation;
    f.mirror_x = mirror;
    f.matrix = active.matrix;
    f.range = active.color_range;
    f.transfer = active.transfer;
    f.primaries = active.primaries;
    auto &info = f.frame;
    info.size = sizeof(info);
    info.version = 1;
    info.sequence = sequence;
    info.generation = active.generation;
    info.width = output.width;
    info.height = output.height;
    info.stride_bytes = output.width * 4;
    info.rgba_bytes = info.stride_bytes * info.height;
    info.received_timestamp_us = active.received_us;
    info.decoded_timestamp_us = active.decoded_us;
    info.pts_valid = active.pts_us >= 0;
    info.presentation_timestamp_us = info.pts_valid ? active.pts_us : 0;
    info.clock_id = ClockId();
    info.clock_domain = HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    info.timestamp_kind = HV_INPUT_TIME_LOCAL_DECODE;
    info.row_origin = HV_INPUT_ROW_TOP_LEFT;
    info.decode_mode = 2; // MediaCodec PRIVATE -> Vulkan RGBA
  }
  void Run() {
    std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
      changed.wait(lock, [this] {
        return closing || (pending.image && slots.CanBegin());
      });
      if (closing)
        break;
      int index = slots.Begin();
      if (index < 0) {
        pending.Reset();
        continue;
      }
      active.TakeFrom(pending);
      auto seq = pending_sequence;
      working = true;
      const bool capture_requested = capture.Begin();
      lock.unlock();
      bool ok = false;
      try {
        ok = gpu.Convert(active, index, rotation, mirror, capture_requested);
      } catch (...) {
        gpu.error = "exception during native Vulkan conversion";
      }
      if (!ok)
        source.SetGpuError("native Vulkan image service", gpu.error.c_str());
      lock.lock();
      if (gpu.unfenced) {
        // An uncertain submit or failed wait is terminal. Quarantine the
        // AImage, reader and context. Release remains BUSY until process
        // teardown.
        closing = true;
        changed.notify_all();
        return;
      }
      if (ok && !closing && !draining && !source.stop &&
          active.generation == generation) {
        Fill(gpu.slots[index], seq);
        slots.Publish(index, generation, seq, true, true);
        if (capture_requested) {
          if (gpu.CaptureVisible()) {
            capture_info = gpu.slots[index].frame.frame;
            capture_info.width = gpu.CaptureWidth();
            capture_info.height = gpu.CaptureHeight();
            capture_info.stride_bytes = capture_info.width * 4;
            capture_info.rgba_bytes =
                capture_info.stride_bytes * capture_info.height;
          }
          capture.Finish(gpu.CaptureVisible());
        }
      } else
        slots.Discard(index);
      active.Reset();
      working = false;
      if (!ok)
        closing = true;
      changed.notify_all();
    }
    pending.Reset();
    slots.Invalidate();
    done = true;
    changed.notify_all();
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
    s.generation = image.generation;
  }
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
  s.pending.Reset();
  s.changed.notify_all();
}
bool NativeVulkanImageService::Retired() {
  auto &s = *state_;
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.done && !s.capture_copying && s.slots.Retired();
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
} // namespace hvinput
