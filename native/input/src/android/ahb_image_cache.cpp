#include "input_image_cache.h"
namespace hvinput {
const char* CacheFailureName(CacheFailure reason) {
 switch(reason){case CacheFailure::None:return "none";case CacheFailure::NullBuffer:return "null_buffer";case CacheFailure::RemovedBuffer:return "removed_buffer";case CacheFailure::ContractMismatch:return "contract_mismatch";case CacheFailure::CreateFailed:return "create_failed";case CacheFailure::Full:return "cache_full";}return "unknown";
}
void InputImageCache::Fail(CacheFailure reason,void* buffer,uint64_t generation,const ImageContract& contract,uint32_t slot) {
 failure_={};failure_.reason=reason;failure_.buffer=buffer;failure_.generation=generation;failure_.contract=contract;failure_.matched_slot=slot;
 if(slot<entries_.size()) {
  const auto& e=entries_[slot];const auto& c=e.contract;failure_.generation_changed=e.generation!=generation;
  const uint64_t requested[]={contract.external_format,contract.format,contract.features,contract.model,contract.range,contract.x_chroma,contract.y_chroma,contract.width,contract.height,contract.components[0],contract.components[1],contract.components[2],contract.components[3]};
  const uint64_t previous[]={c.external_format,c.format,c.features,c.model,c.range,c.x_chroma,c.y_chroma,c.width,c.height,c.components[0],c.components[1],c.components[2],c.components[3]};
  for(uint32_t i=0;i<13;++i)if(requested[i]!=previous[i])failure_.contract_diff|=1u<<i;
 }
 for(uint32_t i=0;i<entries_.size();++i){const auto& e=entries_[i];auto& s=failure_.slots[i];s.entry=e;if(e.live){s.complete=!e.last_fence||backend_.Complete(e.last_fence);++failure_.live;failure_.removed+=e.removed;failure_.complete+=s.complete;failure_.pinned+=!s.complete;}}
}
bool ImageContract::operator==(const ImageContract& b) const {
 if(external_format!=b.external_format||format!=b.format||features!=b.features||model!=b.model||range!=b.range||x_chroma!=b.x_chroma||y_chroma!=b.y_chroma||width!=b.width||height!=b.height)return false;
 for(int i=0;i<4;++i)if(components[i]!=b.components[i])return false;return true;
}
CacheEntry* InputImageCache::Acquire(void* buffer,uint64_t generation,const ImageContract& contract) {
 failure_.reason=CacheFailure::None;
 if(!buffer){Fail(CacheFailure::NullBuffer,buffer,generation,contract);return nullptr;}
 for(auto& e:entries_) if(e.live&&e.buffer==buffer) {
  auto slot=static_cast<uint32_t>(&e-entries_.data());
  if(e.removed){Fail(CacheFailure::RemovedBuffer,buffer,generation,contract,slot);return nullptr;}
  if(e.generation==generation&&e.contract==contract)return &e;
  e.removed=true;
  if(e.last_fence&&!backend_.Complete(e.last_fence)){Fail(CacheFailure::ContractMismatch,buffer,generation,contract,slot);Collect();return nullptr;}
  Collect();break; // The old import is retired; allocate its replacement below.
 }
 for(auto& e:entries_)if(!e.live) {
  e={};e.buffer=buffer;e.generation=generation;e.contract=contract;
  if(!backend_.Create(e)){Fail(CacheFailure::CreateFailed,buffer,generation,contract,static_cast<uint32_t>(&e-entries_.data()));e={};return nullptr;}e.live=true;return &e;
 }Fail(CacheFailure::Full,buffer,generation,contract);return nullptr;
}
void InputImageCache::Remove(void* buffer){for(auto& e:entries_)if(e.live&&e.buffer==buffer)e.removed=true;}
void InputImageCache::RemoveAll(){for(auto& e:entries_)if(e.live)e.removed=true;}
void InputImageCache::Collect(){for(auto& e:entries_)if(e.live&&e.removed&&(!e.last_fence||backend_.Complete(e.last_fence))){backend_.Destroy(e);e={};}}
uint32_t InputImageCache::Live()const{uint32_t n=0;for(const auto& e:entries_)n+=e.live;return n;}
}
