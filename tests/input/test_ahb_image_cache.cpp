#include "input_image_cache.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace hvinput;
struct FakeImporter : ImageImporter {
 int creates=0,destroys=0; uint64_t completed=0;
 bool Create(CacheEntry&) override { ++creates; return true; }
 void Destroy(CacheEntry&) override { ++destroys; }
 bool Complete(uint64_t fence) override { return fence<=completed; }
};
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(0)
int main(int argc,char** argv) { try {
 FakeImporter backend; InputImageCache cache(backend); ImageContract contract{};
 void* buffer=reinterpret_cast<void*>(1);
 if(argc!=2)return 2;
 if(!std::strcmp(argv[1],"SameBufferReusesImport")) {
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
 } else return 2;
 std::puts("PASS");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;} }
