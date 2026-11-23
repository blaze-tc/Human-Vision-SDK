#include "humanvision_input_native.h"
#include <algorithm>
#include <android/hardware_buffer.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
template <class T> T Abi() {
  T value{};
  value.size = sizeof(value);
  value.version = 1;
  return value;
}
uint32_t Hash(const std::vector<uint8_t> &bytes) {
  uint32_t h = 2166136261u;
  for (auto x : bytes)
    h = (h ^ x) * 16777619u;
  return h;
}
int Failure(HV_InputHandle h, const char *where, int status) {
  char error[512]{};
  if (h)
    HV_Input_GetLastError(h, error, sizeof(error));
  std::fprintf(stderr, "%s failed status=%d detail=%s\n", where, status, error);
  return 1;
}
bool ReadyProof(int fd) {
  if (fd == -1)
    return true;
  pollfd item{fd, POLLIN, 0};
  const bool ready = poll(&item, 1, 1000) == 1 && (item.revents & POLLIN) &&
                     !(item.revents & (POLLERR | POLLNVAL));
  close(fd);
  return ready;
}
int Release(HV_InputHandle h, const HV_InputNativeFrameV1 &f) {
  return HV_Input_ReleaseNativeFrameV1(h, f.slot, f.frame.generation,
                                       f.frame.sequence);
}
} // namespace
int main(int argc, char **argv) {
  if (argc != 5) {
    std::fprintf(stderr, "Usage: native_input_smoke RTSP_URL "
                         "HARDWARE_AVC_CODEC SOURCE_WIDTH SOURCE_HEIGHT\n");
    return 2;
  }
  const uint32_t width =
      static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
  const uint32_t height =
      static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
  auto options = Abi<HV_InputNativeOptionsV1>();
  options.source = Abi<HV_InputOptions>();
  options.source.url = argv[1];
  options.source.max_width = width;
  options.source.max_height = height;
  options.source.transport_tcp = 1;
  options.source.timeout_ms = 8000;
  options.source.reconnect_delay_ms = 200;
  options.hardware_avc_codec = argv[2];
  options.width = 512;
  options.height = 288;
  options.enable_readback = 1;
  char error[512]{};
  HV_InputHandle h = nullptr;
  int rc = HV_Input_OpenNativeV1(&options, &h, error, sizeof(error));
  if (rc) {
    std::fprintf(stderr, "open failed status=%d detail=%s\n", rc, error);
    return 1;
  }
  auto device = Abi<HV_InputNativeDeviceV1>();
  auto pool = Abi<HV_InputNativePoolV1>();
  if (HV_Input_GetNativeDeviceV1(h, &device) ||
      HV_Input_GetNativePoolV1(h, &pool) || pool.slot_count != 3 ||
      pool.width != 512 || pool.height != 288 ||
      pool.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM)
    return Failure(h, "device/pool", rc);
  std::printf("independent_instance=1 vendor=%x family=%u device_uuid=",
              device.vendor_id, device.queue_family);
  for (auto b : device.device_uuid)
    std::printf("%02x", b);
  std::printf(" driver_uuid=");
  for (auto b : device.driver_uuid)
    std::printf("%02x", b);
  std::puts("");
  uint64_t after = 0;
  int positive_fds = 0;
  HV_InputNativeFrameV1 held[3]{};
  for (auto &f : held) {
    f = Abi<HV_InputNativeFrameV1>();
    rc = HV_Input_WaitNativeFrameV1(h, after, 12000, &f);
    if (rc || f.frame.sequence <= after || f.slot >= 3 ||
        f.hardware_buffer != pool.hardware_buffers[f.slot])
      return Failure(h, "wait/lease", rc);
    positive_fds += f.producer_sync_fd >= 0;
    if (!ReadyProof(f.producer_sync_fd))
      return Failure(h, "producer sync proof", rc);
    after = f.frame.sequence;
  }
  auto frame = Abi<HV_InputNativeFrameV1>();
  rc = HV_Input_WaitNativeFrameV1(h, after, 200, &frame);
  if (rc != HV_INPUT_NO_FRAME)
    return Failure(h, "three held slots must bound publication", rc);
  for (int i = 0; i < 2; ++i)
    if (Release(h, held[i]))
      return Failure(h, "return slot", rc);
  uint64_t request = 0;
  if (HV_Input_RequestNativeCaptureV1(h, &request))
    return Failure(h, "request full crop", rc);
  auto photo = Abi<HV_InputFrameInfo>();
  std::vector<uint8_t> analysis(pool.width * pool.height * 4);
  bool matched = false;
  for (int attempt = 0; attempt < 60; ++attempt) {
    frame = Abi<HV_InputNativeFrameV1>();
    rc = HV_Input_WaitNativeFrameV1(h, after, 1500, &frame);
    if (rc)
      return Failure(h, "wait after pressure", rc);
    after = frame.frame.sequence;
    positive_fds += frame.producer_sync_fd >= 0;
    if (!ReadyProof(frame.producer_sync_fd))
      return Failure(h, "analysis sync", rc);
    rc = HV_Input_CopyNativeCaptureV1(h, request, nullptr, 0, &photo);
    if (rc == HV_INPUT_BUFFER_TOO_SMALL) {
      std::vector<uint8_t> hd(photo.rgba_bytes);
      if (HV_Input_CopyNativeCaptureV1(h, request, hd.data(), hd.size(),
                                       &photo))
        return Failure(h, "copy HD", rc);
      if (photo.sequence == after &&
          photo.generation == frame.frame.generation) {
        if (HV_Input_CopyNativeRgbaV1(h, frame.slot, frame.frame.generation,
                                      after, analysis.data(),
                                      analysis.size()) ||
            photo.width != width || photo.height != height ||
            photo.row_origin != HV_INPUT_ROW_TOP_LEFT ||
            frame.frame.row_origin != HV_INPUT_ROW_TOP_LEFT ||
            frame.frame.clock_id != photo.clock_id ||
            frame.frame.decoded_timestamp_us != photo.decoded_timestamp_us)
          return Failure(h, "paired geometry/identity", rc);
        for (size_t i = 3; i < hd.size(); i += 4)
          if (hd[i] != 255)
            return Failure(h, "HD alpha", rc);
        for (size_t i = 3; i < analysis.size(); i += 4)
          if (analysis[i] != 255)
            return Failure(h, "analysis alpha", rc);
        std::printf("analysis=%ux%u hash=%08x capture=%ux%u hash=%08x "
                    "generation=%llu seq=%llu positive_sync_fds=%d\n",
                    frame.frame.width, frame.frame.height, Hash(analysis),
                    photo.width, photo.height, Hash(hd),
                    (unsigned long long)photo.generation,
                    (unsigned long long)photo.sequence, positive_fds);
        matched = true;
      } else if (HV_Input_RequestNativeCaptureV1(h, &request))
        return Failure(h, "retry capture pairing", rc);
    } else if (rc != HV_INPUT_NO_FRAME)
      return Failure(h, "capture state", rc);
    if (Release(h, frame))
      return Failure(h, "return analysis", rc);
    if (matched)
      break;
  }
  if (!matched)
    return Failure(h, "exact HD-analysis pair", rc);
  if (HV_Input_Close(h) || HV_Input_Release(h) != HV_INPUT_BUSY)
    return Failure(h, "close with held lease", rc);
  if (Release(h, held[2]))
    return Failure(h, "release last lease", rc);
  for (int attempt = 0; attempt < 1200; ++attempt) {
    rc = HV_Input_Release(h);
    if (!rc) {
      std::puts("closed_after_actual_retirement=1");
      return 0;
    }
    if (rc != HV_INPUT_BUSY)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return Failure(h, "retire", rc);
}
