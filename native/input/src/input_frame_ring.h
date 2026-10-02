#pragma once
#include <array>
#include <cstdint>
namespace hvinput {
enum class InputSlotState { Free, Acquired, CopyQueued, Published, Retiring };
struct InputFrameSlot { InputSlotState state=InputSlotState::Free; uint64_t generation=0,sequence=0,fence=0; bool observed=false; };
class InputFrameRing {
 std::array<InputFrameSlot,3> slots_{}; uint64_t generation_=0; uint64_t drops_=0; int published_=-1;
public:
 void Begin(uint64_t generation); int Acquire(uint64_t generation,uint64_t sequence);
 bool Queue(int slot,uint64_t fence); bool Complete(int slot,uint64_t completed);
 void Observe(int slot,uint64_t sequence); void Release(int slot,uint64_t sequence); void Cancel(int slot); void Close(); void Collect(uint64_t completed);
 int Published()const{return published_;} uint64_t Drops()const{return drops_;}
 uint32_t Live()const; const InputFrameSlot& Slot(int index)const{return slots_[index];}
};
class InputEncodedBacklog {
 bool waiting_=true; uint64_t deadline_=0;
public:
 void Reset(uint64_t deadline){waiting_=true;deadline_=deadline;}
 bool Admit(bool keyframe); bool Expired(uint64_t now)const{return waiting_&&now>=deadline_;}
 bool Waiting()const{return waiting_;}
};
}
