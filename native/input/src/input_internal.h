#pragma once
#include "humanvision_input.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#ifdef _WIN32
#include <libswscale/swscale.h>
#endif
}
namespace hvinput {
int64_t NowUs();
uint64_t ClockId();
void InstallSafeLog();
class NativeVulkanImageService;
struct Session {
#ifdef __ANDROID__
  std::shared_ptr<NativeVulkanImageService> native_output;
  std::string hardware_codec;
#endif
  std::string url;
  uint32_t max_width = 0, max_height = 0, timeout_ms = 0,
           reconnect_delay_ms = 0;
  bool tcp = true;
  std::atomic<uint32_t> active_copies{0};
  // Decoder-output and acquired-image counts, never inference/claim surrogates.
  std::atomic<uint64_t> decoded_frames{0}, acquired_frames{0};
  std::atomic<uint32_t> live_consumers{0};
  int last_width = 0, last_height = 0, last_format = -1, last_matrix = -1,
      last_range = -1, last_transfer = -1, last_primaries = -1;
  std::atomic<bool> stop{false};
  std::atomic<int64_t> deadline{0};
  std::atomic<uint32_t> state{HV_INPUT_OPENING};
  std::mutex mutex;
  std::condition_variable wake;
  std::thread worker;
  std::atomic<bool> worker_done{false};
  std::string error;
  // Protected by mutex; retain the first native GPU failure through decoder close.
  bool gpu_error_recorded = false;
  std::vector<uint8_t> latest, scratch;
  HV_InputFrameInfo info{};
  static int Interrupt(void *);
  void Run() noexcept;
  void Decode();
  void SetError(const char *stage, int code);
  void SetGpuError(const char *stage, const char *detail);
  bool SetDecoderState(uint32_t next);
#ifdef _WIN32
  void Publish(AVFrame *, SwsContext *&, AVRational, int64_t received_us);
#endif
};
} // namespace hvinput
struct HV_InputSessionOpaque : hvinput::Session {};
