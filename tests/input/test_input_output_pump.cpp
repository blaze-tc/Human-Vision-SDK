#include "input_decode_timing.h"
#include "input_output_pump.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "DecodeTimingByPts") == 0) {
    hvinput::InputDecodeTiming timing;
    timing.RecordInput(3000, true, 100);
    timing.RecordInput(1000, true, 200);
    timing.RecordInput(2000, true, 300);
    timing.RecordInput(2000, true, 400);
    timing.RecordInput(0, false, 450);
    timing.RecordOutput(1000, 600);
    timing.RecordOutput(2000, 650);
    timing.RecordOutput(0, 700);
    timing.RecordOutput(999, 800);
    timing.RecordOutput(3000, 900);
    timing.RecordSkippedOutput();
    const auto first = timing.TakeWindow();
    CHECK(first.samples == 2 && first.total_us == 1200 && first.max_us == 800);
    CHECK(first.ambiguous_outputs == 1 && first.invalid_outputs == 1 && first.unmatched_outputs == 1);
    CHECK(first.invalid_inputs == 1 && first.skipped_outputs == 1 && first.pending_inputs == 1);
    timing.RecordOutput(2000, 1000);
    const auto second = timing.TakeWindow();
    CHECK(second.samples == 0 && second.ambiguous_outputs == 1 && second.pending_inputs == 0);
    std::puts("PASS: codec timing uses PTS across reordered output and report windows");
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "SurfaceReleaseCadence") == 0) {
    hvinput::InputSurfaceReleasePacer pacer;
    CHECK(pacer.Ready(0));
    pacer.RecordRelease(1000);
    CHECK(!pacer.Ready(24999));
    CHECK(pacer.Ready(25000));
    CHECK(pacer.Ready(51000));
    // The next interval starts at the real release, with no burst catch-up.
    pacer.RecordRelease(51000);
    CHECK(!pacer.Ready(74999));
    CHECK(pacer.Ready(75000));
    std::puts("PASS: Surface releases stay at least 24 ms apart");
    return 0;
  }
  // Simulate a decoder completing after its input was queued. No second packet
  // is sent: output must still be drained while the network producer is idle.
  std::mutex mutex;
  std::condition_variable ready;
  std::atomic<bool> decoded{false}, failed{false};
  int consumed=0;
  {
    hvinput::InputOutputPump pump([&] {
      if (decoded.exchange(false)) {
        std::lock_guard<std::mutex> lock(mutex); ++consumed; ready.notify_all();
      }
    }, [&] { failed=true; ready.notify_all(); });
    decoded=true;
    std::unique_lock<std::mutex> lock(mutex);
    CHECK(ready.wait_for(lock,std::chrono::seconds(1),[&]{return consumed==1||failed.load();}));
    CHECK(consumed==1 && !failed);
  }
  // Reconnection destroys the pump before codec/reader resources; no callback
  // may survive destruction. Idle polling must sleep rather than busy-spin.
  std::atomic<unsigned> calls{0};
  for (int i=0;i<16;++i) {
    hvinput::InputOutputPump pump([&]{++calls;},[&]{failed=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    pump.StopAndJoin(); pump.StopAndJoin();
    const auto stopped=calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(calls==stopped);
  }
  CHECK(calls>0 && calls<1024 && !failed);
  {
    hvinput::InputOutputPump pump([]{throw std::runtime_error("decoder failure");},[&]{
      std::lock_guard<std::mutex> lock(mutex); failed=true; ready.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    CHECK(ready.wait_for(lock,std::chrono::seconds(1),[&]{return failed.load();}));
  }
  std::puts("PASS: independent output, bounded idle, reconnect join, exception forwarding");
}
