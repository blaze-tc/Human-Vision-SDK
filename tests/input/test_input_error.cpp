#include "input_internal.h"
#include <cstdio>
#include <stdexcept>
using namespace hvinput;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
namespace {
enum class RunHookMode { Fatal, Ordinary, Close };
RunHookMode run_mode=RunHookMode::Fatal;
int hook_count=0;
bool ordinary_observed=false;
}
namespace hvinput {
void RunReconnectTestHook(Session& s) {
 ++hook_count;
 if(run_mode==RunHookMode::Fatal) s.SetGpuError("Run-window GPU failure","Vulkan result -2");
 else if(run_mode==RunHookMode::Close || hook_count==2) {
  ordinary_observed=s.state==HV_INPUT_RECONNECTING;
  HV_Input_Close(static_cast<HV_InputSessionOpaque*>(&s));
 }
}
}
static void VerifyActualRunTransitions() {
 auto configure=[](HV_InputSessionOpaque& s){s.url="rtsp://127.0.0.1:0/fixture";s.timeout_ms=100;s.reconnect_delay_ms=0;};
 HV_InputSessionOpaque fatal;configure(fatal);run_mode=RunHookMode::Fatal;hook_count=0;
 std::thread fatal_worker([&]{fatal.Run();});fatal_worker.join();
 CHECK(hook_count==1);CHECK(fatal.worker_done);CHECK(fatal.stop);CHECK(fatal.state==HV_INPUT_FAILED);
 CHECK(fatal.error=="Run-window GPU failure: Vulkan result -2");
 HV_InputSessionOpaque ordinary;configure(ordinary);run_mode=RunHookMode::Ordinary;hook_count=0;ordinary_observed=false;
 std::thread ordinary_worker([&]{ordinary.Run();});ordinary_worker.join();
 CHECK(hook_count==2);CHECK(ordinary_observed);CHECK(ordinary.info.generation==2);CHECK(ordinary.worker_done);CHECK(!ordinary.gpu_error_recorded);CHECK(ordinary.state==HV_INPUT_RECONNECTING);
 HV_InputSessionOpaque closed;configure(closed);run_mode=RunHookMode::Close;hook_count=0;
 std::thread close_worker([&]{closed.Run();});close_worker.join();
 CHECK(hook_count==1);CHECK(closed.stop);CHECK(closed.worker_done);CHECK(closed.state==HV_INPUT_OPENING);CHECK(!closed.gpu_error_recorded);
 HV_InputSessionOpaque prior;configure(prior);prior.SetGpuError("prior GPU failure","Vulkan result -3");hook_count=0;prior.Run();CHECK(hook_count==0);CHECK(prior.info.generation==0);CHECK(prior.state==HV_INPUT_FAILED);CHECK(prior.worker_done);
}
int main(){try{
 VerifyActualRunTransitions();
 Session s;
 s.SetGpuError("import AHB memory","Vulkan result -2");
 CHECK(s.error=="import AHB memory: Vulkan result -2");CHECK(s.gpu_error_recorded);CHECK(s.stop);CHECK(s.state==HV_INPUT_FAILED);
 // Actual stop-interrupt/reconnect writer after the GPU writer, on its worker thread.
 std::thread interrupted([&]{s.SetError("read compressed H264",AVERROR_EXIT);});interrupted.join();
 CHECK(s.error=="import AHB memory: Vulkan result -2");
 s.SetError("RTSP worker failure",AVERROR_UNKNOWN);CHECK(s.error=="import AHB memory: Vulkan result -2");
 s.SetGpuError("buffer removed notification capacity","Vulkan result -13");CHECK(s.error=="import AHB memory: Vulkan result -2");
 std::thread reconnect([&]{s.SetError("read compressed H264",AVERROR_EXIT);s.SetDecoderState(HV_INPUT_RECONNECTING);s.SetDecoderState(HV_INPUT_STREAMING);});reconnect.join();CHECK(s.state==HV_INPUT_FAILED);
 Session cache;
 cache.SetGpuError("AHB import cache","cache_full (live=16 removed=0 complete=16 pinned=0)");cache.SetError("read compressed H264",AVERROR_EXIT);CHECK(cache.error=="AHB import cache: cache_full (live=16 removed=0 complete=16 pinned=0)");
 Session decoder;
 decoder.SetError("first demux error",AVERROR(EIO));auto first=decoder.error;
 decoder.SetError("second demux error",AVERROR(EINVAL));CHECK(decoder.error!=first);CHECK(!decoder.gpu_error_recorded);
 CHECK(decoder.SetDecoderState(HV_INPUT_RECONNECTING));CHECK(decoder.state==HV_INPUT_RECONNECTING);CHECK(decoder.SetDecoderState(HV_INPUT_STREAMING));CHECK(decoder.state==HV_INPUT_STREAMING);
 decoder.SetGpuError("create actual YCbCr conversion","Vulkan result -3");CHECK(decoder.error=="create actual YCbCr conversion: Vulkan result -3");
 Session clean;CHECK(!clean.gpu_error_recorded);CHECK(!clean.stop);CHECK(clean.state==HV_INPUT_OPENING);clean.stop=true;CHECK(!clean.SetDecoderState(HV_INPUT_STREAMING));CHECK(clean.state==HV_INPUT_OPENING);
 Session later_gpu;CHECK(later_gpu.SetDecoderState(HV_INPUT_STREAMING));later_gpu.SetGpuError("queue color conversion","Vulkan result -4");CHECK(!later_gpu.SetDecoderState(HV_INPUT_RECONNECTING));CHECK(later_gpu.state==HV_INPUT_FAILED);
 std::puts("PASS");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
