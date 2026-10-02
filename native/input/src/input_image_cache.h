#pragma once
#include <array>
#include <cstdint>
namespace hvinput {
struct ImageContract {
 uint64_t external_format=0; uint32_t format=0,features=0,model=0,range=0,x_chroma=0,y_chroma=0,width=0,height=0;
 uint32_t components[4]{};
 bool operator==(const ImageContract&) const;
};
struct CacheEntry {
 void* buffer=nullptr; uint64_t generation=0,last_fence=0; ImageContract contract{};
 bool live=false,removed=false; void* resource=nullptr;
};
struct ImageImporter {
 virtual ~ImageImporter()=default;
 virtual bool Create(CacheEntry&)=0;
 virtual void Destroy(CacheEntry&)=0;
 virtual bool Complete(uint64_t)=0;
};
// Fixed storage keeps entry addresses stable and bounds the active decoder pool.
// Backend completion is a real GPU fence in production; frame count is never a fence.
class InputImageCache {
 ImageImporter& backend_; std::array<CacheEntry,16> entries_{};
public:
 explicit InputImageCache(ImageImporter& backend):backend_(backend){}
 CacheEntry* Acquire(void* buffer,uint64_t generation,const ImageContract&);
 void Used(CacheEntry& e,uint64_t fence){e.last_fence=fence;}
 void Remove(void* buffer);
 void RemoveAll(); void Collect(); uint32_t Live() const;
};
}
