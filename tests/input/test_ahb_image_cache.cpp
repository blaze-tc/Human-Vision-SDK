#include "input_image_cache.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace hvinput;
struct FakeImporter : ImageImporter {
 int creates=0,destroys=0; uint64_t completed=0; bool fail_create=false;
 bool Create(CacheEntry&) override { ++creates; return !fail_create; }
 void Destroy(CacheEntry&) override { ++destroys; }
 bool Complete(uint64_t fence) override { return fence<=completed; }
};
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main(int argc,char** argv) { try {
 FakeImporter backend; InputImageCache cache(backend); ImageContract contract{};
 void* buffer=reinterpret_cast<void*>(1);
 if(argc!=2)return 2;
 if(!std::strcmp(argv[1],"SeventeenthLiveObjectAdmits")) {
  // Exact observed condition: same contract/generation, all prior imports complete,
  // no buffer removal. Unit importer models completion, never physical media/fences.
  CacheEntry* first=nullptr;
  for(uintptr_t i=1;i<=17;++i){auto* e=cache.Acquire(reinterpret_cast<void*>(i),1,contract);CHECK(e);if(i==1)first=e;cache.Used(*e,i);backend.completed=i;}
  CHECK(cache.Live()==17);CHECK(backend.creates==17);CHECK(backend.destroys==0);CHECK(cache.Acquire(buffer,1,contract)==first);CHECK(backend.creates==17);
 } else if(!std::strcmp(argv[1],"SupportedDomainReuses64AndRejects65")) {
  static_assert(kInputDecoderBufferCapacity==64);
  std::array<CacheEntry*,64> entries{};
  for(uintptr_t i=1;i<=64;++i){entries[i-1]=cache.Acquire(reinterpret_cast<void*>(i),1,contract);CHECK(entries[i-1]);cache.Used(*entries[i-1],i);backend.completed=i;}
  for(int reuse=0;reuse<10;++reuse)for(uintptr_t i=1;i<=64;++i)CHECK(cache.Acquire(reinterpret_cast<void*>(i),1,contract)==entries[i-1]);
  CHECK(backend.creates==64);CHECK(backend.destroys==0);CHECK(!cache.Acquire(reinterpret_cast<void*>(65),1,contract));CHECK(cache.LastFailure().reason==CacheFailure::Full);CHECK(cache.LastFailure().live==64);CHECK(cache.LastFailure().complete==64);CHECK(cache.LastFailure().pinned==0);CHECK(cache.LastFailure().slots.size()==64);
  cache.RemoveAll();cache.Collect();CHECK(cache.Live()==0);CHECK(backend.destroys==64);
 } else if(!std::strcmp(argv[1],"SameBufferReusesImport")) {
  for(int i=0;i<100;++i) { auto* e=cache.Acquire(buffer,1,contract); CHECK(e); cache.Used(*e,i+1);backend.completed=i+1;cache.Collect(); }
  CHECK(backend.creates==1);CHECK(backend.destroys==0);cache.Remove(buffer);cache.Collect();CHECK(backend.destroys==1);
 } else if(!std::strcmp(argv[1],"RemovedBufferWaitsForFence")) {
  auto* e=cache.Acquire(buffer,1,contract);CHECK(e);cache.Used(*e,8);cache.Remove(buffer);cache.Collect();CHECK(backend.destroys==0);CHECK(!cache.Acquire(buffer,1,contract));
  backend.completed=8;cache.Collect();CHECK(backend.destroys==1);CHECK(cache.Live()==0);
 } else if(!std::strcmp(argv[1],"ContractChangeRetiresOldImport")) {
  auto* e=cache.Acquire(buffer,1,contract);CHECK(e);cache.Used(*e,9);contract.model=3;
  CHECK(!cache.Acquire(buffer,1,contract));CHECK(backend.destroys==0);backend.completed=9;cache.Collect();e=cache.Acquire(buffer,1,contract);CHECK(e);CHECK(backend.creates==2);CHECK(backend.destroys==1);
  cache.Used(*e,10);contract.range=1;CHECK(!cache.Acquire(buffer,1,contract));CHECK(backend.destroys==1);backend.completed=10;cache.Collect();e=cache.Acquire(buffer,1,contract);CHECK(e);CHECK(backend.creates==3);
  cache.Used(*e,11);CHECK(!cache.Acquire(buffer,2,contract));CHECK(backend.destroys==2);backend.completed=11;cache.Collect();CHECK(cache.Acquire(buffer,2,contract));CHECK(backend.creates==4);CHECK(backend.destroys==3);
 } else if(!std::strcmp(argv[1],"CompletedContractRebuildsInSameAcquire")) {
  auto* e=cache.Acquire(buffer,1,contract);CHECK(e);cache.Used(*e,9);backend.completed=9;
  contract.model=3;e=cache.Acquire(buffer,1,contract);CHECK(e);CHECK(e->contract==contract);CHECK(backend.creates==2);CHECK(backend.destroys==1);CHECK(cache.Live()==1);CHECK(cache.LastFailure().reason==CacheFailure::None);
  e=cache.Acquire(buffer,2,contract);CHECK(e);CHECK(e->generation==2);CHECK(backend.creates==3);CHECK(backend.destroys==2);
  cache.Used(*e,10);contract.range=1;CHECK(!cache.Acquire(buffer,2,contract));CHECK(backend.destroys==2);CHECK(cache.Live()==1);CHECK(cache.LastFailure().reason==CacheFailure::ContractMismatch);
  backend.completed=10;e=cache.Acquire(buffer,2,contract);CHECK(!e);CHECK(cache.LastFailure().reason==CacheFailure::RemovedBuffer);cache.Collect();e=cache.Acquire(buffer,2,contract);CHECK(e);CHECK(backend.creates==4);CHECK(backend.destroys==3);
 } else if(!std::strcmp(argv[1],"FailuresCarryPreciseSnapshot")) {
  CHECK(!cache.Acquire(nullptr,1,contract));CHECK(cache.LastFailure().reason==CacheFailure::NullBuffer);
  auto* e=cache.Acquire(buffer,1,contract);CHECK(e);CHECK(cache.LastFailure().reason==CacheFailure::None);
  cache.Used(*e,9);cache.Remove(buffer);CHECK(!cache.Acquire(buffer,1,contract));
  const auto& removed=cache.LastFailure();CHECK(removed.reason==CacheFailure::RemovedBuffer);CHECK(removed.live==1);CHECK(removed.removed==1);CHECK(removed.pinned==1);CHECK(removed.complete==0);CHECK(removed.slots[0].entry.last_fence==9);CHECK(!removed.slots[0].complete);
  backend.completed=9;cache.Collect();CHECK(cache.Live()==0);
  e=cache.Acquire(buffer,1,contract);CHECK(e);cache.Used(*e,10);
  ImageContract changed=contract;changed.external_format=17;changed.format=3;changed.features=5;changed.model=2;changed.range=1;changed.x_chroma=1;changed.y_chroma=1;changed.width=640;changed.height=360;for(int i=0;i<4;++i)changed.components[i]=i+1;
  CHECK(!cache.Acquire(buffer,2,changed));
  const auto& mismatch=cache.LastFailure();CHECK(mismatch.reason==CacheFailure::ContractMismatch);CHECK(mismatch.buffer==buffer);CHECK(mismatch.generation==2);CHECK(mismatch.contract==changed);CHECK(mismatch.matched_slot==0);CHECK(mismatch.slots[0].entry.contract==contract);CHECK(mismatch.slots[0].entry.generation==1);CHECK(mismatch.contract_diff==0x1fff);CHECK(mismatch.generation_changed);CHECK(mismatch.pinned==1);CHECK(backend.destroys==1);
  backend.completed=10;cache.Collect();backend.fail_create=true;CHECK(!cache.Acquire(buffer,2,changed));
  const auto& create=cache.LastFailure();CHECK(create.reason==CacheFailure::CreateFailed);CHECK(create.matched_slot==0);CHECK(create.slots[0].entry.buffer==buffer);CHECK(create.slots[0].entry.contract==changed);CHECK(cache.Live()==0);
  backend.fail_create=false;
  for(uintptr_t i=1;i<=kInputDecoderBufferCapacity;++i){e=cache.Acquire(reinterpret_cast<void*>(i),2,contract);CHECK(e);cache.Used(*e,10+i);}
  CHECK(!cache.Acquire(reinterpret_cast<void*>(kInputDecoderBufferCapacity+1),2,contract));
  const auto& full=cache.LastFailure();CHECK(full.reason==CacheFailure::Full);CHECK(full.live==kInputDecoderBufferCapacity);CHECK(full.removed==0);CHECK(full.pinned==kInputDecoderBufferCapacity);CHECK(full.complete==0);CHECK(full.buffer==reinterpret_cast<void*>(kInputDecoderBufferCapacity+1));CHECK(full.matched_slot==UINT32_MAX);
  backend.completed=10+kInputDecoderBufferCapacity;CHECK(!cache.Acquire(reinterpret_cast<void*>(kInputDecoderBufferCapacity+1),2,contract));CHECK(cache.LastFailure().pinned==0);CHECK(cache.LastFailure().complete==kInputDecoderBufferCapacity);CHECK(cache.Live()==kInputDecoderBufferCapacity);CHECK(backend.destroys==2);
  e=cache.Acquire(buffer,2,contract);CHECK(e);CHECK(cache.LastFailure().reason==CacheFailure::None);CHECK(backend.creates==3+kInputDecoderBufferCapacity);
 } else return 2;
 std::puts("PASS");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;} }
