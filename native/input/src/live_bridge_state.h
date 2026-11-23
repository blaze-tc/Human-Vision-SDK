#pragma once
#include "android/native_output_slots.h"
#include <array>
#include <cstdint>
namespace hvinput {
// Caller serializes every operation. This is the actual Unity-target lease and
// callback ledger; producer completion never retires a consumer's sampling lease.
class LiveBridgeState {
 NativeOutputSlots slots_;
 struct Identity{uint64_t generation{},sequence{};};
 std::array<Identity,3> identities_{};
 uint64_t generation_{},target_{};
 int writing_=-1;
 bool bound_{},retiring_{},closing_{};
 unsigned queued_{};
public:
 bool Bind(uint64_t target){if(!target||closing_||!TargetsRetired())return false;target_=target;bound_=true;retiring_=false;return true;}
 bool QueueEvent(){if(queued_)return false;++queued_;return true;}
 void EventFinished(){if(queued_)--queued_;}
 void AdmitGeneration(uint64_t generation){if(generation>generation_){generation_=generation;slots_.Invalidate();}}
 int Begin(uint64_t generation,uint64_t sequence){
  if(!bound_||retiring_||closing_||writing_>=0||!generation||!sequence||generation<generation_)return -1;
  AdmitGeneration(generation);int slot=slots_.Begin();if(slot<0)return -1;
  identities_[slot]={generation,sequence};writing_=slot;return slot;
 }
 bool Finish(bool complete){
  if(writing_<0||!complete)return false;
  int slot=writing_;writing_=-1;const auto id=identities_[slot];
  if(closing_||retiring_||id.generation!=generation_){slots_.Discard(slot);return false;}
  return slots_.Publish(slot,id.generation,id.sequence,true,true);
 }
 int Acquire(uint64_t after){return !closing_&&!retiring_?slots_.Acquire(after):-1;}
 bool Release(unsigned slot,uint64_t generation,uint64_t sequence){return slots_.Release(slot,generation,sequence);}
 uint64_t Sequence(unsigned slot)const{return slot<3?identities_[slot].sequence:0;}
 uint64_t Target()const{return target_;}
 bool Writing()const{return writing_>=0;}
 bool CanBegin()const{return bound_&&!retiring_&&!closing_&&writing_<0&&slots_.CanBegin();}
 void RetireTargets(){retiring_=true;bound_=false;slots_.Invalidate();}
 bool TargetsRetired()const{return writing_<0&&slots_.Retired();}
 void Close(){closing_=true;RetireTargets();}
 bool Closing()const{return closing_;}
 bool Retiring()const{return retiring_;}
 bool Retired()const{return closing_&&TargetsRetired()&&!queued_;}
};
}
