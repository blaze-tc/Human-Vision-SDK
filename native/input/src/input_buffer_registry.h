#pragma once
#include "input_image_cache.h"
#include <mutex>
namespace hvinput {
struct BufferIdentity { const void* reader=nullptr; void* buffer=nullptr; };
struct BufferRegistryStats {
 uint32_t live=0,reserved=0,committed=0,pending=0;
 uint64_t known_notifications=0,duplicate_notifications=0,unknown_notifications=0;
};
// Object identities, never allocation IDs. Callback only compares these values.
// The caller retains a decoded lease through reservation/import; cache ownership
// retains registered AHB objects through fence-driven destruction.
class InputBufferRegistry {
 struct Entry {BufferIdentity identity{};bool committed=false,seen=false,pending=false;};
 mutable std::mutex mutex_;
 std::array<Entry,kInputDecoderBufferCapacity> entries_{};
 uint64_t known_=0,duplicates_=0,unknown_=0;
 Entry* Find(const void* reader,void* buffer) {
  for(auto& e:entries_)if(e.identity.reader==reader&&e.identity.buffer==buffer&&buffer)return &e;
  return nullptr;
 }
public:
 bool Reserve(const void* reader,void* buffer) {
  if(!reader||!buffer)return false;
  std::lock_guard<std::mutex> lock(mutex_);
  // The import cache owns at most one entry for an AHB object across domains.
  for(const auto& e:entries_)if(e.identity.buffer==buffer)return false;
  for(auto& e:entries_)if(!e.identity.buffer){e={};e.identity={reader,buffer};return true;}
  return false;
 }
 bool Commit(const void* reader,void* buffer) {
  std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(reader,buffer);if(!e)return false;e->committed=true;return true;
 }
 bool Unregister(const void* reader,void* buffer) {
  std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(reader,buffer);if(!e)return false;*e={};return true;
 }
 void Notify(const void* reader,void* buffer) {
  std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(reader,buffer);
  if(!e){++unknown_;return;}++known_;if(e->seen){++duplicates_;return;}e->seen=e->pending=true;
 }
 bool TakeRemoved(BufferIdentity& identity) {
  std::lock_guard<std::mutex> lock(mutex_);
  for(auto& e:entries_)if(e.committed&&e.pending){identity=e.identity;e.pending=false;return true;}
  return false;
 }
 BufferRegistryStats Inspect()const {
  std::lock_guard<std::mutex> lock(mutex_);BufferRegistryStats s{};s.known_notifications=known_;s.duplicate_notifications=duplicates_;s.unknown_notifications=unknown_;
  for(const auto& e:entries_)if(e.identity.buffer){++s.live;s.committed+=e.committed;s.reserved+=!e.committed;s.pending+=e.pending;}return s;
 }
};
// Roll back reservation on a failed/throwing Create. Successful imports unregister
// explicitly before their Vulkan/AHB destruction, without holding this mutex.
class InputBufferReservation {
 InputBufferRegistry& registry_;BufferIdentity identity_;bool active_=false,committed_=false;
public:
 InputBufferReservation(InputBufferRegistry& registry,const void* reader,void* buffer):registry_(registry),identity_{reader,buffer},active_(registry.Reserve(reader,buffer)){}
 InputBufferReservation(const InputBufferReservation&)=delete;
 InputBufferReservation& operator=(const InputBufferReservation&)=delete;
 ~InputBufferReservation(){if(active_&&!committed_)registry_.Unregister(identity_.reader,identity_.buffer);}
 explicit operator bool()const{return active_;}
 bool Commit(){if(!active_)return false;committed_=registry_.Commit(identity_.reader,identity_.buffer);return committed_;}
};
}
