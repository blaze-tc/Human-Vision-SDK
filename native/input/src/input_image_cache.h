#pragma once
#include <array>
#include <cstdint>
namespace hvinput {
// Named API26-era supported BufferQueue object domain; not an allocation-count
// guarantee for every Android vendor/future queue. Above this domain fails explicitly.
constexpr uint32_t kInputDecoderBufferCapacity=64;
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
enum class CacheFailure { None, NullBuffer, RemovedBuffer, ContractMismatch, CreateFailed, Full };
const char* CacheFailureName(CacheFailure);
// Failure-only, fixed storage: no per-frame allocation or snapshot traversal.
struct CacheSlotSnapshot { CacheEntry entry{}; bool complete=false; };
struct CacheFailureSnapshot {
 CacheFailure reason=CacheFailure::None; void* buffer=nullptr; uint64_t generation=0;
 ImageContract contract{}; uint32_t matched_slot=UINT32_MAX,contract_diff=0;
 bool generation_changed=false; uint32_t live=0,removed=0,complete=0,pinned=0;
 std::array<CacheSlotSnapshot,kInputDecoderBufferCapacity> slots{};
};
// Fixed storage keeps entry addresses stable and bounds the active decoder pool.
// Backend completion is a real GPU fence in production; frame count is never a fence.
class InputImageCache {
 ImageImporter& backend_; std::array<CacheEntry,kInputDecoderBufferCapacity> entries_{};
 CacheFailureSnapshot failure_{};
 void Fail(CacheFailure,void*,uint64_t,const ImageContract&,uint32_t slot=UINT32_MAX);
public:
 explicit InputImageCache(ImageImporter& backend):backend_(backend){}
 CacheEntry* Acquire(void* buffer,uint64_t generation,const ImageContract&);
 const CacheFailureSnapshot& LastFailure()const{return failure_;}
 void Used(CacheEntry& e,uint64_t fence){e.last_fence=fence;}
 void Remove(void* buffer);
 void RemoveAll(); void Collect(); uint32_t Live() const;
};
}
