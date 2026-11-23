#pragma once
#include "android/native_output_slots.h"
#include "input_dimensions.h"
#include <algorithm>
#include <cstdint>

namespace hvinput {
struct NativeOutputReservation { int analysis=-1, live=-1; };
inline bool CanConfigureNativeLiveMetadata(bool configured,bool working,
                                           bool closing,bool draining,bool stopped) {
  return (!working||!configured)&&!closing&&!draining&&!stopped;
}
// Independent pools never acquire an analysis publication to render preview.
inline NativeOutputReservation ReserveNativeOutputs(NativeOutputSlots& analysis,
                                                     NativeOutputSlots& live,
                                                     bool live_enabled) {
  return {analysis.CanBegin()?analysis.Begin():-1,
          live_enabled&&live.CanBegin()?live.Begin():-1};
}
struct NativeLiveFit { uint32_t width=0,height=0; };
inline bool ValidNativeLiveOptions(uint32_t width, uint32_t height, uint32_t rotation,
                                   uint32_t mirror, uint32_t enabled, uint32_t reserved) {
  return ValidInputBounds(width,height) && mirror<=1 && enabled<=1 && !reserved &&
      (rotation==0 || rotation==90 || rotation==180 || rotation==270);
}
inline bool FitNativeLive(uint32_t width,uint32_t height,uint32_t rotation,
                          uint32_t max_width,uint32_t max_height,
                          NativeLiveFit& result) {
  result={};
  if(!width||!height||!ValidInputBounds(max_width,max_height)||
     (rotation!=0&&rotation!=90&&rotation!=180&&rotation!=270))return false;
  if(rotation==90||rotation==270)std::swap(width,height);
  return FitInputDimensions(width,height,max_width,max_height,result.width,result.height);
}
inline bool CanPublishNativeGeneration(uint64_t active,uint64_t admitted,
                                       bool closing,bool draining,bool stopped) {
  return active&&active==admitted&&!closing&&!draining&&!stopped;
}
} // namespace hvinput
