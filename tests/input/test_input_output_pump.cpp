#include "input_output_pump.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); return 1; } } while (0)

int main() {
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
