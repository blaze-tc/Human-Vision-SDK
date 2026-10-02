#include "humanvision_input.h"
extern "C" {
#include <libavutil/log.h>
}
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (0)
using namespace std::chrono;
struct StallServer {
  SOCKET listener = INVALID_SOCKET;
  std::atomic<SOCKET> client{INVALID_SOCKET};
  std::atomic<int> accepted{0};
  std::atomic<bool> stop{false};
  std::thread thread;
  unsigned short port = 0;
  StallServer() {
    WSADATA data;
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener, (sockaddr *)&address, sizeof(address)) == 0);
    int len = sizeof(address);
    CHECK(getsockname(listener, (sockaddr *)&address, &len) == 0);
    port = ntohs(address.sin_port);
    CHECK(listen(listener, 4) == 0);
    thread = std::thread([this] {
      while (!stop) {
        SOCKET s = accept(listener, nullptr, nullptr);
        if (s == INVALID_SOCKET)
          break;
        client = s;
        ++accepted;
        while (!stop && client == s)
          std::this_thread::sleep_for(milliseconds(2));
        closesocket(s);
      }
    });
  }
  void Disconnect() {
    SOCKET s = client.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET)
      shutdown(s, SD_BOTH);
  }
  ~StallServer() {
    stop = true;
    Disconnect();
    closesocket(listener);
    if (thread.joinable())
      thread.join();
    WSACleanup();
  }
  std::string Url() {
    return "rtsp://user:secret@127.0.0.1:" + std::to_string(port) + "/stall";
  }
};
HV_InputOptions Options(const std::string &url) {
  return {sizeof(HV_InputOptions), 1, url.c_str(), 1920, 1080, 300, 20, 1};
}
uint32_t State(HV_InputHandle h) {
  uint32_t s = 99;
  CHECK(HV_Input_GetState(h, &s) == 0);
  return s;
}
void Retire(HV_InputHandle h) {
  CHECK(HV_Input_Close(h) == 0);
  auto end = steady_clock::now() + seconds(3);
  int r;
  do {
    r = HV_Input_Release(h);
    if (r == HV_INPUT_BUSY)
      std::this_thread::sleep_for(milliseconds(2));
  } while (r == HV_INPUT_BUSY && steady_clock::now() < end);
  CHECK(r == 0);
}
void StalledReadCanBeCancelled() {
  StallServer server;
  auto url = server.Url();
  auto o = Options(url);
  o.timeout_ms = 10000;
  HV_InputHandle h = nullptr;
  CHECK(HV_Input_Open(&o, &h) == 0);
  CHECK(HV_Input_Release(h) == HV_INPUT_BUSY);
  auto end = steady_clock::now() + seconds(2);
  while (server.accepted == 0 && steady_clock::now() < end)
    std::this_thread::sleep_for(milliseconds(2));
  CHECK(server.accepted > 0);
  auto start = steady_clock::now();
  CHECK(HV_Input_Close(h) == 0);
  CHECK(steady_clock::now() - start < milliseconds(50));
  CHECK(State(h) == HV_INPUT_CLOSING || State(h) == HV_INPUT_STOPPED);
  Retire(h);
  CHECK(steady_clock::now() - start < seconds(2));
}
void SequenceAndGenerationOnReconnect() {
  StallServer server;
  auto url = server.Url();
  auto o = Options(url);
  HV_InputHandle h = nullptr;
  CHECK(HV_Input_Open(&o, &h) == 0);
  auto end = steady_clock::now() + seconds(4);
  while (server.accepted < 2 && steady_clock::now() < end) {
    server.Disconnect();
    std::this_thread::sleep_for(milliseconds(25));
  }
  CHECK(server.accepted >= 2);
  HV_InputFrameInfo f{};
  f.size = sizeof(f);
  f.version = 1;
  CHECK(HV_Input_PollFrame(h, 0, &f) == HV_INPUT_NO_FRAME);
  CHECK(f.generation >= 2);
  CHECK(f.sequence == 0);
  Retire(h);
}
void ErrorsRedactCredentials() {
  StallServer server;
  auto url = server.Url();
  auto o = Options(url);
  HV_InputHandle h = nullptr;
  CHECK(HV_Input_Open(&o, &h) == 0);
  // Exercise the real shared FFmpeg callback, including decoder-thread
  // messages. CTest rejects either credential substring in captured
  // stdout/stderr.
  av_log(nullptr, AV_LOG_ERROR, "RTSP failure rtsp://user:secret@host/test\n");
  std::thread logging([] {
    av_log(nullptr, AV_LOG_ERROR, "decoder rtsp://user:secret@host/test\n");
  });
  logging.join();
  std::this_thread::sleep_for(milliseconds(600));
  char error[1024]{};
  CHECK(HV_Input_GetLastError(h, error, sizeof(error)) == 0);
  CHECK(std::string(error).size() > 0);
  CHECK(std::string(error).find("secret") == std::string::npos);
  CHECK(std::string(error).find("user:") == std::string::npos);
  Retire(h);
}
void VersionAndClock() {
  auto url = std::string("rtsp://127.0.0.1:1/test");
  auto o = Options(url);
  HV_InputHandle h = nullptr;
  o.version = 2;
  CHECK(HV_Input_Open(&o, &h) == HV_INPUT_INVALID);
  CHECK(h == nullptr);
  o.version = 1;
  o.size = 1;
  CHECK(HV_Input_Open(&o, &h) == HV_INPUT_INVALID);
  HV_InputClockInfo c{};
  c.size = sizeof(c);
  c.version = 1;
  CHECK(HV_Input_QueryClock(&c) == 0);
  CHECK(c.clock_id != 0);
  CHECK(c.clock_domain == HV_INPUT_CLOCK_NATIVE_MONOTONIC);
  auto first = c.now_us;
  std::this_thread::sleep_for(milliseconds(3));
  CHECK(HV_Input_QueryClock(&c) == 0);
  CHECK(c.now_us > first);
}
void ColorContractAdvancesGeneration();
void PrimariesOnlyChangeAdvancesGeneration();
void ClosingWaitsForCopy();
void LegacyRtspAbi();
int main(int argc, char **argv) {
  try {
    CHECK(argc == 2);
    std::string n = argv[1];
    if (n == "StalledReadCanBeCancelled")
      StalledReadCanBeCancelled();
    else if (n == "SequenceAndGenerationOnReconnect")
      SequenceAndGenerationOnReconnect();
    else if (n == "ErrorsRedactCredentials")
      ErrorsRedactCredentials();
    else if (n == "VersionAndClock")
      VersionAndClock();
#ifdef HV_INPUT_POLICY_TESTS
    else if (n == "ColorContractAdvancesGeneration")
      ColorContractAdvancesGeneration();
    else if (n == "PrimariesOnlyChangeAdvancesGeneration")
      PrimariesOnlyChangeAdvancesGeneration();
    else if (n == "ClosingWaitsForCopy")
      ClosingWaitsForCopy();
#endif

#ifdef HV_INPUT_LEGACY_TESTS
    else if (n == "LegacyRtspAbi")
      LegacyRtspAbi();
#endif
    else
      throw std::runtime_error("unknown test");
    std::cout << "PASS " << n << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << "\n";
    return 1;
  }
}
#ifdef HV_INPUT_POLICY_TESTS
#include "input_internal.h"
using PixelPattern = std::array<uint8_t, 16>;
void SetPixels(AVFrame *frame, const PixelPattern &pattern) {
  for (int row = 0; row < 2; ++row)
    std::memcpy(frame->data[0] + row * frame->linesize[0],
                pattern.data() + row * 8, 8);
}
void CheckMetadata(const HV_InputFrameInfo &actual,
                   const HV_InputFrameInfo &expected) {
#define CHECK_FIELD(field) CHECK(actual.field == expected.field)
  CHECK_FIELD(size);
  CHECK_FIELD(version);
  CHECK_FIELD(sequence);
  CHECK_FIELD(generation);
  CHECK_FIELD(width);
  CHECK_FIELD(height);
  CHECK_FIELD(stride_bytes);
  CHECK_FIELD(rgba_bytes);
  CHECK_FIELD(received_timestamp_us);
  CHECK_FIELD(decoded_timestamp_us);
  CHECK_FIELD(presentation_timestamp_us);
  CHECK_FIELD(clock_id);
  CHECK_FIELD(clock_domain);
  CHECK_FIELD(timestamp_kind);
  CHECK_FIELD(pts_valid);
  CHECK_FIELD(row_origin);
  CHECK_FIELD(color_space);
  CHECK_FIELD(decode_mode);
#undef CHECK_FIELD
}
void ColorContractAdvancesGeneration() {
  HV_InputSessionOpaque session;
  session.max_width = 1920;
  session.max_height = 1080;
  session.info.size = sizeof(session.info);
  session.info.version = 1;
  session.info.generation = 1;
  AVFrame *frame = av_frame_alloc();
  CHECK(frame);
  frame->width = 2;
  frame->height = 2;
  frame->format = AV_PIX_FMT_RGBA;
  frame->color_range = AVCOL_RANGE_MPEG;
  frame->colorspace = AVCOL_SPC_BT709;
  frame->color_trc = AVCOL_TRC_BT709;
  frame->color_primaries = AVCOL_PRI_BT709;
  CHECK(av_frame_get_buffer(frame, 1) == 0);
  const PixelPattern first{11, 23, 37, 255, 41,  53,  67,  191,
                           71, 83, 97, 127, 101, 113, 137, 63};
  const PixelPattern second{139, 151, 163, 51,  173, 181, 193, 103,
                            199, 211, 223, 157, 227, 233, 241, 251};
  SetPixels(frame, first);
  frame->best_effort_timestamp = 10;
  SwsContext *scaler = nullptr;
  session.Publish(frame, scaler, AVRational{1, 1000}, 1);
  CHECK(session.info.sequence == 1);
  HV_InputFrameInfo copied{};
  copied.size = sizeof(copied);
  copied.version = 1;
  PixelPattern pixels{};
  auto expected = session.info;
  CHECK(HV_Input_CopyRgba(&session, 1, pixels.data(), pixels.size(), &copied) ==
        0);
  CHECK(pixels == first);
  CheckMetadata(copied, expected);
  auto old = session.info.generation;
  frame->color_range = AVCOL_RANGE_JPEG;
  frame->best_effort_timestamp = 20;
  SetPixels(frame, second);
  session.Publish(frame, scaler, AVRational{1, 1000}, 2);
  CHECK(session.info.generation > old);
  CHECK(session.info.sequence == 2);
  expected = session.info;
  pixels.fill(0);
  CHECK(HV_Input_CopyRgba(&session, 1, pixels.data(), pixels.size(), &copied) ==
        HV_INPUT_NO_FRAME);
  CHECK(HV_Input_CopyRgba(&session, 2, pixels.data(), pixels.size(), &copied) ==
        0);
  CHECK(pixels == second);
  CheckMetadata(copied, expected);
  CHECK(copied.sequence == 2);
  CHECK(copied.generation == session.info.generation);
  CHECK(copied.width == 2 && copied.height == 2);
  CHECK(copied.presentation_timestamp_us == 20000);
  CHECK(copied.pts_valid == 1);
  frame->color_trc = AVCOL_TRC_IEC61966_2_1;
  session.Publish(frame, scaler, AVRational{1, 1000}, 3);
  CHECK(session.info.color_space == HV_INPUT_COLOR_SRGB);
  frame->color_primaries = AVCOL_PRI_BT2020;
  session.Publish(frame, scaler, AVRational{1, 1000}, 4);
  CHECK(session.info.color_space == 0);
  frame->color_primaries = AVCOL_PRI_UNSPECIFIED;
  session.Publish(frame, scaler, AVRational{1, 1000}, 5);
  CHECK(session.info.color_space == 0);
  session.stop = true;
  CHECK(HV_Input_CopyRgba(&session, session.info.sequence, pixels.data(),
                          pixels.size(), &copied) == HV_INPUT_NO_FRAME);
  sws_freeContext(scaler);
  av_frame_free(&frame);
}
void PrimariesOnlyChangeAdvancesGeneration() {
  HV_InputSessionOpaque session;
  session.max_width = session.max_height = 2;
  AVFrame *frame = av_frame_alloc();
  CHECK(frame);
  frame->width = frame->height = 2;
  frame->format = AV_PIX_FMT_RGBA;
  frame->color_range = AVCOL_RANGE_JPEG;
  frame->colorspace = AVCOL_SPC_BT709;
  frame->color_trc = AVCOL_TRC_IEC61966_2_1;
  frame->color_primaries = AVCOL_PRI_BT709;
  CHECK(av_frame_get_buffer(frame, 1) == 0);
  SetPixels(frame, PixelPattern{1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10,
                                11, 12, 255});
  SwsContext *scaler = nullptr;
  session.Publish(frame, scaler, AVRational{1, 1000}, 1);
  auto generation = session.info.generation;
  frame->color_primaries = AVCOL_PRI_BT2020;
  session.Publish(frame, scaler, AVRational{1, 1000}, 2);
  const bool changed = session.info.generation > generation;
  sws_freeContext(scaler);
  av_frame_free(&frame);
  CHECK(changed);
}
#endif

#ifdef HV_INPUT_POLICY_TESTS
void ClosingWaitsForCopy() {
  HV_InputSessionOpaque session;
  session.worker = std::thread([] {});
  const bool worker_retired =
      WaitForSingleObject(session.worker.native_handle(), 2000) ==
      WAIT_OBJECT_0;
  std::unique_lock<std::mutex> lock(session.mutex);
  std::thread copying([&] {
    HV_InputFrameInfo f{};
    f.size = sizeof(f);
    f.version = 1;
    uint8_t bytes[4];
    HV_Input_CopyRgba(&session, 1, bytes, 4, &f);
  });
  const auto deadline = steady_clock::now() + seconds(2);
  while (session.active_copies == 0 && steady_clock::now() < deadline)
    std::this_thread::yield();
  const bool copy_active = session.active_copies != 0;
  HV_Input_Close(&session);
  uint32_t state = State(&session);
  lock.unlock();
  copying.join();
  session.worker.join();
  CHECK(worker_retired);
  CHECK(copy_active);
  CHECK(state == HV_INPUT_CLOSING);
}
#endif

#ifdef HV_INPUT_LEGACY_TESTS
#include "humanvision/humanvision_rtsp.h"
#include <type_traits>
void LegacyRtspAbi() {
  static_assert(
      std::is_same_v<decltype(&HV_RtspOpen),
                     void *(__cdecl *)(const char *, int, int, int, int)>);
  static_assert(
      std::is_same_v<decltype(&HV_RtspCopyFrame),
                     int(__cdecl *)(void *, int64_t, void *, int, int *, int *,
                                    int64_t *, int64_t *)>);
  static_assert(
      std::is_same_v<decltype(&HV_RtspState), int(__cdecl *)(void *)>);
  static_assert(
      std::is_same_v<decltype(&HV_RtspClose), void(__cdecl *)(void *)>);
  CHECK(HV_RtspOpen(nullptr, 1920, 1080, 1, 100) == nullptr);
  CHECK(HV_RtspOpen("file:///test", 1920, 1080, 1, 100) == nullptr);
  CHECK(HV_RtspState(nullptr) == 4);
  CHECK(HV_RtspCopyFrame(nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr,
                         nullptr) == -1);
  HV_RtspClose(nullptr);
}
#endif
