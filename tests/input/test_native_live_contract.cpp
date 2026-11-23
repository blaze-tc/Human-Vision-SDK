#include "humanvision_input_live.h"
#include <cstddef>
#include <cstdio>
#include <type_traits>

#if __has_include("native_live_contract.h")
#include "native_live_contract.h"
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); return 1; } } while (false)
using namespace hvinput;

static_assert(std::is_standard_layout<HV_InputLiveApiV1>::value);
static_assert(offsetof(HV_InputLiveConfigV1, max_width) == 8);
static_assert(offsetof(HV_InputLiveGeometryV1, generation) == 8);
static_assert(offsetof(HV_InputSourceProgressV1, generation) == 8);
static_assert(sizeof(HV_InputLiveConfigV1) == 32);

int main() {
  // This is the actual production configuration gate used by ConfigureLive.
  // First attach publishes metadata only; the active analysis submission owns
  // its prior disabled-live snapshot and no live GPU allocation is replaced.
  CHECK(CanConfigureNativeLiveMetadata(false,true,false,false,false));
  CHECK(!CanConfigureNativeLiveMetadata(true,true,false,false,false));
  CHECK(CanConfigureNativeLiveMetadata(true,false,false,false,false));
  CHECK(!CanConfigureNativeLiveMetadata(false,true,true,false,false));
  CHECK(!CanConfigureNativeLiveMetadata(false,true,false,true,false));
  CHECK(!CanConfigureNativeLiveMetadata(false,true,false,false,true));
  NativeLiveFit fit{};
  // Zero bounds mean the actual visible crop, never a model or 1080p cap.
  CHECK(FitNativeLive(1920,1080,0,0,0,fit) && fit.width==1920 && fit.height==1080);
  CHECK(FitNativeLive(3840,2160,0,0,0,fit) && fit.width==3840 && fit.height==2160);
  CHECK(FitNativeLive(4096,2160,0,0,0,fit) && fit.width==4096 && fit.height==2160);
  CHECK(FitNativeLive(1920,1080,90,0,0,fit) && fit.width==1080 && fit.height==1920);
  CHECK(!FitNativeLive(7680,4320,0,0,0,fit)); // Explicit unsupported; never downsample original mode.
  CHECK(!FitNativeLive(1920,1080,0,1920,0,fit));
  CHECK(ValidNativeLiveOptions(0,0,0,0,1,0));
  CHECK(ValidNativeLiveOptions(3840,2160,0,0,1,0));
  CHECK(ValidNativeLiveOptions(4096,2160,90,1,1,0));
  CHECK(!ValidNativeLiveOptions(0,1080,0,0,1,0));
  CHECK(!ValidNativeLiveOptions(5000,3000,0,0,1,0));
  CHECK(!ValidNativeLiveOptions(0,0,45,0,1,0));
  CHECK(!ValidNativeLiveOptions(0,0,0,0,1,1));
  uint32_t decoder_width=0,decoder_height=0;
  CHECK(ResolveInputDecoderHint(0,0,1920,1080,decoder_width,decoder_height) && decoder_width==1920 && decoder_height==1080);
  CHECK(ResolveInputDecoderHint(0,0,3840,2160,decoder_width,decoder_height) && decoder_width==3840 && decoder_height==2160);
  CHECK(ResolveInputDecoderHint(512,288,1920,1080,decoder_width,decoder_height) && decoder_width==512 && decoder_height==288);
  CHECK(!ResolveInputDecoderHint(0,0,0,0,decoder_width,decoder_height));
  CHECK(!ResolveInputDecoderHint(0,1080,1920,1080,decoder_width,decoder_height));
  CHECK(FitNativeLive(1920,1080,0,1920,1080,fit) && fit.width==1920 && fit.height==1080);
  CHECK(FitNativeLive(3840,2160,0,1920,1080,fit) && fit.width==1920 && fit.height==1080);
  CHECK(FitNativeLive(1920,1080,90,1920,1080,fit) && fit.width==607 && fit.height==1080);
  CHECK(FitNativeLive(640,480,0,1920,1080,fit) && fit.width==640 && fit.height==480);
  CHECK(!FitNativeLive(0,1080,0,1920,1080,fit));
  CHECK(!FitNativeLive(1920,1080,45,1920,1080,fit));
  CHECK(!FitNativeLive(1920,1080,0,0,1080,fit));
  CHECK(!CanPublishNativeGeneration(2,3,false,false,false));
  CHECK(!CanPublishNativeGeneration(2,2,true,false,false));
  CHECK(!CanPublishNativeGeneration(2,2,false,true,false));
  CHECK(!CanPublishNativeGeneration(2,2,false,false,true));
  CHECK(CanPublishNativeGeneration(2,2,false,false,false));

  // Model claims may hold every analysis target. The production reservation
  // selector must still admit the independent live lane, with no analysis claim.
  NativeOutputSlots analysis, live;
  for (uint64_t sequence=1;sequence<=3;++sequence) {
    int slot=analysis.Begin();CHECK(slot>=0);
    CHECK(analysis.Publish(slot,1,sequence,true,true));CHECK(analysis.Acquire(0)==slot);
  }
  CHECK(!analysis.CanBegin());
  for (uint64_t sequence=1;sequence<=60;++sequence) {
    auto work=ReserveNativeOutputs(analysis,live,true);
    CHECK(work.analysis<0 && work.live>=0);
    CHECK(live.Publish(work.live,1,sequence,true,true));
  }
  // Sixty controlled source ticks represent a two-second 30-Hz producer gap.
  // This is a latest-only protocol check, not a hardware FPS measurement.
  int newest=live.Acquire(0);CHECK(newest>=0 && live.Leased(newest,1,60));
  CHECK(!live.Release(newest,2,60) && !live.Release(newest,1,59));
  live.Invalidate();CHECK(live.Leased(newest,1,60));CHECK(!live.Retired());
  CHECK(live.Release(newest,1,60) && live.Retired());

  // A full live consumer pool cannot block analysis. Disabling live also does
  // not pause, clear or acquire the analysis lane.
  NativeOutputSlots free_analysis, held_live;
  int held[3]{};
  for (uint64_t sequence=1;sequence<=3;++sequence) {
    held[sequence-1]=held_live.Begin();CHECK(held[sequence-1]>=0);
    CHECK(held_live.Publish(held[sequence-1],1,sequence,true,true));
    CHECK(held_live.Acquire(0)==held[sequence-1]);
  }
  CHECK(!held_live.CanBegin());
  auto work=ReserveNativeOutputs(free_analysis,held_live,true);
  CHECK(work.analysis>=0 && work.live<0);
  CHECK(free_analysis.Publish(work.analysis,1,4,true,true));
  int observed=free_analysis.Acquire(0);CHECK(observed>=0);
  CHECK(free_analysis.Release(observed,1,4));
  CHECK(held_live.Release(held[0],1,1));
  work=ReserveNativeOutputs(free_analysis,held_live,false);
  CHECK(work.analysis>=0 && work.live<0);
  free_analysis.Discard(work.analysis);
  CHECK(held_live.CanBegin());

  // Fence proof and current generation gate publication, not receipt of an
  // encoded packet or a consumer/model request.
  NativeOutputSlots proof;
  int writing=proof.Begin();CHECK(writing>=0);
  CHECK(!proof.Publish(writing,2,70,false,true));
  CHECK(proof.Acquire(0)<0 && !proof.Retired());
  CHECK(!CanPublishNativeGeneration(2,3,false,false,false));
  proof.Discard(writing);CHECK(proof.Retired());
  writing=proof.Begin();CHECK(proof.Publish(writing,3,71,true,true));
  int leased=proof.Acquire(0);proof.Invalidate();
  CHECK(!proof.Retired() && proof.Leased(leased,3,71));
  CHECK(proof.Release(leased,3,71) && proof.Retired());
  std::puts("PASS live/analysis independent reservation, latest, ownership and generation contract");
  return 0;
}
#else
int main() {
  std::fputs("FAIL native live production contract is missing\n", stderr);
  return 1;
}
#endif
