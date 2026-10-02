#include "input_image_cache.h"
namespace hvinput {
bool ImageContract::operator==(const ImageContract& b) const {
 if(external_format!=b.external_format||format!=b.format||features!=b.features||model!=b.model||range!=b.range||x_chroma!=b.x_chroma||y_chroma!=b.y_chroma||width!=b.width||height!=b.height)return false;
 for(int i=0;i<4;++i)if(components[i]!=b.components[i])return false;return true;
}
CacheEntry* InputImageCache::Acquire(void* buffer,uint64_t generation,const ImageContract& contract) {
 if(!buffer)return nullptr;
 for(auto& e:entries_) if(e.live&&e.buffer==buffer) {
  if(e.removed)return nullptr;
  if(e.generation==generation&&e.contract==contract)return &e;
  e.removed=true;Collect();return nullptr;
 }
 for(auto& e:entries_)if(!e.live) {
  e={};e.buffer=buffer;e.generation=generation;e.contract=contract;
  if(!backend_.Create(e)){e={};return nullptr;}e.live=true;return &e;
 }return nullptr;
}
void InputImageCache::Remove(void* buffer){for(auto& e:entries_)if(e.live&&e.buffer==buffer)e.removed=true;}
void InputImageCache::RemoveAll(){for(auto& e:entries_)if(e.live)e.removed=true;}
void InputImageCache::Collect(){for(auto& e:entries_)if(e.live&&e.removed&&(!e.last_fence||backend_.Complete(e.last_fence))){backend_.Destroy(e);e={};}}
uint32_t InputImageCache::Live()const{uint32_t n=0;for(const auto& e:entries_)n+=e.live;return n;}
}
