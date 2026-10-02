#include "input_frame_ring.h"
namespace hvinput {
void InputFrameRing::Begin(uint64_t g){Close();generation_=g;}
int InputFrameRing::Acquire(uint64_t g,uint64_t s){if(!g||g!=generation_)return -1;for(int i=0;i<3;++i)if(slots_[i].state==InputSlotState::Free){slots_[i]={InputSlotState::Acquired,g,s,0};return i;}++drops_;return -1;}
bool InputFrameRing::Queue(int s,uint64_t f){if(s<0||s>=3||!f||slots_[s].state!=InputSlotState::Acquired)return false;slots_[s].fence=f;slots_[s].state=InputSlotState::CopyQueued;return true;}
bool InputFrameRing::Complete(int s,uint64_t f){if(s<0||s>=3)return false;auto& slot=slots_[s];if(slot.state!=InputSlotState::CopyQueued||slot.fence>f||slot.generation!=generation_)return false;if(published_>=0&&published_!=s){auto& previous=slots_[published_];if(previous.state==InputSlotState::Published&&!previous.observed){previous.state=InputSlotState::Retiring;++drops_;}}
slot.state=InputSlotState::Published;published_=s;return true;}
void InputFrameRing::Observe(int s,uint64_t sequence){if(s<0||s>=3)return;auto& slot=slots_[s];if(slot.state==InputSlotState::Published&&slot.sequence==sequence&&slot.generation==generation_)slot.observed=true;}
void InputFrameRing::Release(int s,uint64_t sequence){if(s<0||s>=3)return;auto& slot=slots_[s];if(slot.sequence!=sequence||slot.state!=InputSlotState::Published)return;slot.state=InputSlotState::Retiring;if(published_==s)published_=-1;}
void InputFrameRing::Cancel(int s){if(s>=0&&s<3&&slots_[s].state==InputSlotState::Acquired)slots_[s]={};}
void InputFrameRing::Close(){published_=-1;generation_=0;for(auto& slot:slots_)if(slot.state!=InputSlotState::Free)slot.state=InputSlotState::Retiring;}
void InputFrameRing::Collect(uint64_t f){for(auto& slot:slots_)if(slot.state==InputSlotState::Retiring&&slot.fence<=f)slot={};}
uint32_t InputFrameRing::Live()const{uint32_t n=0;for(const auto& slot:slots_)if(slot.state!=InputSlotState::Free)++n;return n;}
bool InputEncodedBacklog::Admit(bool keyframe){if(waiting_&&!keyframe)return false;if(keyframe)waiting_=false;return true;}
}
