#include "input_buffer_registry.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
using namespace hvinput;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
static void* Ptr(uintptr_t i){return reinterpret_cast<void*>(i);}
struct RegistryImporter:ImageImporter {
 InputBufferRegistry registry;
 const void* reader=Ptr(1000);
 uint64_t completed=0;
 int creates=0,destroys=0,references=0;
 bool fail=false,notify_during_create=false;
 bool Create(CacheEntry& e)override {
  InputBufferReservation reservation(registry,reader,e.buffer);CHECK(reservation);
  if(notify_during_create){std::thread callback([&]{registry.Notify(reader,e.buffer);});callback.join();BufferIdentity removed;CHECK(!registry.TakeRemoved(removed));}
  if(fail)return false;
  ++references;++creates;CHECK(reservation.Commit());return true;
 }
 void Destroy(CacheEntry& e)override{
  CHECK(registry.Unregister(reader,e.buffer));auto before=registry.Inspect().unknown_notifications;
  // Simulate a synchronous notification during actual resource/ref release.
  // Unregister must have released its mutex already; the object is still owned.
  registry.Notify(reader,e.buffer);CHECK(registry.Inspect().unknown_notifications==before+1);
  --references;++destroys;
 }
 bool Complete(uint64_t fence)override{return fence<=completed;}
 void Poll(InputImageCache& cache){BufferIdentity id;while(registry.TakeRemoved(id))cache.Remove(id.buffer);cache.Collect();}
};
int main(int argc,char** argv){try{
 CHECK(argc==2);
 if(!std::strcmp(argv[1],"KnownUnknownAndDuplicateStorm")) {
  InputBufferRegistry r;for(uintptr_t i=1;i<=64;++i){CHECK(r.Reserve(Ptr(1000),Ptr(i)));CHECK(r.Commit(Ptr(1000),Ptr(i)));}
  std::thread callback([&]{for(int repeat=0;repeat<100;++repeat)for(uintptr_t i=1;i<=64;++i){r.Notify(Ptr(1000),Ptr(i));r.Notify(Ptr(2000),Ptr(i));r.Notify(Ptr(1000),Ptr(i+64));}});callback.join();
  auto s=r.Inspect();CHECK(s.live==64);CHECK(s.pending==64);CHECK(s.known_notifications==6400);CHECK(s.duplicate_notifications==6336);CHECK(s.unknown_notifications==12800);
  BufferIdentity id;int seen=0;while(r.TakeRemoved(id)){CHECK(id.reader==Ptr(1000));++seen;}CHECK(seen==64);CHECK(r.Inspect().pending==0);CHECK(r.Inspect().live==64);
 }else if(!std::strcmp(argv[1],"ReservationCallbackAndRollback")) {
  InputBufferRegistry r;BufferIdentity id;
  {InputBufferReservation reservation(r,Ptr(1000),Ptr(1));CHECK(reservation);std::thread cb([&]{r.Notify(Ptr(1000),Ptr(1));});cb.join();CHECK(r.Inspect().reserved==1);CHECK(!r.TakeRemoved(id));CHECK(reservation.Commit());CHECK(r.TakeRemoved(id));CHECK(r.Unregister(id.reader,id.buffer));}
  {InputBufferReservation failed(r,Ptr(1000),Ptr(2));CHECK(failed);r.Notify(Ptr(1000),Ptr(2));CHECK(!r.TakeRemoved(id));}CHECK(r.Inspect().live==0);r.Notify(Ptr(1000),Ptr(2));CHECK(r.Inspect().unknown_notifications==1);
 }else if(!std::strcmp(argv[1],"BusyFenceKeepsKnownOwnedReference")) {
  RegistryImporter b;InputImageCache cache(b);ImageContract c{};b.notify_during_create=true;
  auto* e=cache.Acquire(Ptr(1),1,c);CHECK(e);cache.Used(*e,8);b.Poll(cache);CHECK(cache.Live()==1);CHECK(b.references==1);CHECK(b.destroys==0);CHECK(b.registry.Inspect().committed==1);CHECK(!cache.Acquire(Ptr(1),1,c));
  b.completed=8;b.Poll(cache);CHECK(b.references==0);CHECK(b.destroys==1);CHECK(cache.Live()==0);CHECK(b.registry.Inspect().live==0);
  b.fail=true;CHECK(!cache.Acquire(Ptr(2),1,c));CHECK(b.registry.Inspect().live==0);CHECK(b.references==0);CHECK(b.creates==1);
 }else if(!std::strcmp(argv[1],"ReaderIdentityIsolationAndReuse")) {
  InputBufferRegistry r;BufferIdentity id;CHECK(r.Reserve(Ptr(1000),Ptr(1)));CHECK(r.Commit(Ptr(1000),Ptr(1)));r.Notify(Ptr(2000),Ptr(1));CHECK(!r.TakeRemoved(id));r.Notify(Ptr(1000),Ptr(1));CHECK(r.TakeRemoved(id));CHECK(r.Unregister(id.reader,id.buffer));
  CHECK(r.Reserve(Ptr(2000),Ptr(1)));CHECK(r.Commit(Ptr(2000),Ptr(1)));r.Notify(Ptr(1000),Ptr(1));CHECK(!r.TakeRemoved(id));r.Notify(Ptr(2000),Ptr(1));CHECK(r.TakeRemoved(id));CHECK(r.Unregister(id.reader,id.buffer));
  // Reused reader address with a different live AHB object; old object callback is unknown.
  // Same reader+AHB pair ABA is excluded by reader callback quiescence and owned AHB refs.
  CHECK(r.Reserve(Ptr(1000),Ptr(2)));CHECK(r.Commit(Ptr(1000),Ptr(2)));r.Notify(Ptr(1000),Ptr(1));CHECK(!r.TakeRemoved(id));r.Notify(Ptr(1000),Ptr(2));CHECK(r.TakeRemoved(id));CHECK(r.Unregister(id.reader,id.buffer));CHECK(r.Inspect().live==0);CHECK(r.Inspect().unknown_notifications==3);
 }else if(!std::strcmp(argv[1],"FixedReservationDomainFailsExplicitly")) {
  InputBufferRegistry r;CHECK(!r.Reserve(nullptr,Ptr(1)));CHECK(!r.Reserve(Ptr(1000),nullptr));for(uintptr_t i=1;i<=64;++i)CHECK(r.Reserve(Ptr(1000),Ptr(i)));CHECK(!r.Reserve(Ptr(1000),Ptr(65)));CHECK(!r.Reserve(Ptr(2000),Ptr(1)));CHECK(!r.Commit(Ptr(1000),Ptr(65)));CHECK(r.Inspect().live==64);CHECK(r.Inspect().reserved==64);
  for(uintptr_t i=1;i<=64;++i)CHECK(r.Unregister(Ptr(1000),Ptr(i)));CHECK(r.Inspect().live==0);
 }else if(!std::strcmp(argv[1],"CloseReopenBalancesKnownResources")) {
  RegistryImporter b;InputImageCache cache(b);ImageContract c{};for(uintptr_t i=1;i<=64;++i){auto* e=cache.Acquire(Ptr(i),1,c);CHECK(e);cache.Used(*e,i);}CHECK(b.references==64);
  cache.RemoveAll();cache.Collect();CHECK(b.references==64);CHECK(b.registry.Inspect().live==64);b.completed=64;cache.Collect();CHECK(b.creates==b.destroys);CHECK(b.references==0);CHECK(b.registry.Inspect().live==0);
  b.reader=Ptr(2000);for(uintptr_t i=1;i<=64;++i){CHECK(cache.Acquire(Ptr(i),2,c));b.registry.Notify(Ptr(1000),Ptr(i));}b.Poll(cache);CHECK(cache.Live()==64);CHECK(b.registry.Inspect().unknown_notifications==128);CHECK(b.references==64);cache.RemoveAll();cache.Collect();CHECK(b.creates==128);CHECK(b.destroys==128);CHECK(b.references==0);CHECK(b.registry.Inspect().live==0);
 }else return 2;
 std::puts("PASS");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
