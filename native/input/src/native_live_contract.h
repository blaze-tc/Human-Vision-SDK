#pragma once
#include "android/native_output_slots.h"
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
inline bool FitNativeLive(uint32_t width,uint32_t height,uint32_t rotation,
                          uint32_t max_width,uint32_t max_height,
                          NativeLiveFit& result) {
  result={};
  if(!width||!height||!max_width||!max_height||
     (rotation!=0&&rotation!=90&&rotation!=180&&rotation!=270))return false;
  if(rotation==90||rotation==270)std::swap(width,height);
  const double scale=std::min(1.0,std::min(double(max_width)/width,double(max_height)/height));
  result={std::max(1u,static_cast<uint32_t>(width*scale)),
          std::max(1u,static_cast<uint32_t>(height*scale))};
  return true;
}
inline bool CanPublishNativeGeneration(uint64_t active,uint64_t admitted,
                                       bool closing,bool draining,bool stopped) {
  return active&&active==admitted&&!closing&&!draining&&!stopped;
}
} // namespace hvinput
