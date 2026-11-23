#include "composition/preview_mailbox.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <limits>
#include <utility>
namespace humanvision::runtime {
namespace {std::atomic<uint64_t> next_snapshot{1};}
PreviewMailbox::PreviewMailbox(AllocationPolicy allocation,ClockSource clock) noexcept :allocation_(allocation),clock_(clock){}
int64_t PreviewMailbox::Now() const noexcept {
 return clock_.now_us?clock_.now_us(clock_.context):std::chrono::duration_cast<std::chrono::microseconds>(
  std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool PreviewMailbox::Valid(const HV_RuntimePreviewV1& i,uint32_t capacity) noexcept {
 return i.struct_size>=sizeof(i)&&i.api_version==1&&i.width&&i.height&&i.width<=4096&&i.height<=4096&&
  uint64_t(i.stride_bytes)>=uint64_t(i.width)*4&&uint64_t(i.rgba_bytes)>=uint64_t(i.stride_bytes)*i.height&&
  i.rgba_bytes<=capacity&&i.source_id&&i.generation&&i.frame_id&&i.config_epoch&&i.timestamp_us>=0;
}
bool PreviewMailbox::Configure(uint32_t bytes) {
 return ConfigureDetailed(bytes)==ConfigureResult::Ready;
}
PreviewMailbox::ConfigureResult PreviewMailbox::ConfigureCandidate(uint32_t bytes) noexcept {
 if(bytes>kCandidateMaximumBytes)return ConfigureResult::BudgetExceeded;
 return ConfigureDetailed(bytes);
}
PreviewMailbox::ConfigureResult PreviewMailbox::ConfigureDetailed(uint32_t bytes) noexcept {
 if(!bytes||bytes>4096u*4096u*4u)return ConfigureResult::Invalid;
 // This storage (including replaced old buffers after a commit) is destroyed
 // outside every admission lock, even on an early return or allocation error.
 std::array<std::vector<uint8_t>,4> replacement;
 bool owns_allocation=false;
 uint32_t previous_capacity=0;
 uint64_t previous_epoch=0,previous_invalidation=0,previous_source=0,previous_generation=0,previous_source_epoch=0;
 try {
  {
   std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
   if(!lock)return ConfigureResult::Busy;
   // A no-op neither replaces storage nor needs any current reader/writer to
   // retire. The other slots remain usable while a prior image is borrowed.
   if(bytes==capacity_)return ConfigureResult::Ready;
   if(allocation_pending_||active_token_||snapshot_writing_||snapshot_readers_)return ConfigureResult::Busy;
   for(const auto& slot:slots_)if(slot.readers)return ConfigureResult::Busy;
   allocation_pending_=owns_allocation=true;
   previous_capacity=capacity_;previous_epoch=capacity_epoch_;previous_invalidation=invalidation_;
   previous_source=source_id_;previous_generation=generation_;previous_source_epoch=source_epoch_;
  }
  // Allocating and initializing optional HD storage must not serialize native
  // source admission. Keep one allocation transaction, but release its mutex.
  bool allocated=true;
  for(auto& buffer:replacement) {
   if(allocation_.allocate)allocation_.allocate(allocation_.context,buffer,bytes);
   else buffer.resize(bytes);
   if(buffer.size()<bytes){allocated=false;break;}
  }
  {
   std::lock_guard<std::mutex> lock(mutex_);allocation_pending_=owns_allocation=false;
   if(!allocated)return ConfigureResult::AllocationFailure;
   // Input, readers and a real in-flight copy could all advance while memory
   // was allocated. Never replace their owners or commit an obsolete epoch.
   if(capacity_!=previous_capacity||capacity_epoch_!=previous_epoch||invalidation_!=previous_invalidation||
      source_id_!=previous_source||generation_!=previous_generation||source_epoch_!=previous_source_epoch||
      active_token_||snapshot_writing_||snapshot_readers_)return ConfigureResult::Busy;
   for(const auto& slot:slots_)if(slot.readers)return ConfigureResult::Busy;
   for(uint32_t i=0;i<3;++i){slots_[i].bytes.swap(replacement[i]);slots_[i].state=State::Free;}
   snapshot_.swap(replacement[3]);capacity_=bytes;++capacity_epoch_;latest_=3;snapshot_candidate_=snapshot_confirmed_=false;
  }
  return ConfigureResult::Ready;
 }catch(...) {
  if(owns_allocation)try{std::lock_guard<std::mutex> lock(mutex_);allocation_pending_=false;}catch(...) {}
  return ConfigureResult::AllocationFailure;
 }
}
bool PreviewMailbox::TryBeginCopy(uint32_t free_credits,bool additional,bool analysis_pending,CopyTicket& out,bool exact) {
 out={};
 std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
 if(!lock||!capacity_||active_token_||analysis_pending||(additional&&free_credits<3)||(!exact&&!CanAttemptLive())) {++skipped_;return false;}
 uint32_t selected=3;
 for(uint32_t i=0;i<3;++i)if(!slots_[i].readers&&i!=latest_&&slots_[i].state!=State::Writing){selected=i;break;}
 if(selected==3&&latest_<3&&!slots_[latest_].readers)selected=latest_;
 if(selected==3){++skipped_;return false;}
 auto& slot=slots_[selected];slot.state=State::Writing;
 active_slot_=selected;active_token_=++next_copy_;copy_invalidation_=invalidation_;active_exact_=exact;
 // Reserve the recovery permit when work is admitted, including a request
 // that later returns BUSY or drains without publishing. Full pools consume none.
 if(!exact&&recovery_copies_)retry_after_us_=Now()+1000000;
 out={active_token_,selected,slot.bytes.data(),capacity_};return true;
}
bool PreviewMailbox::FinishCopy(const CopyTicket& ticket,const HV_RuntimePreviewV1& info,bool complete,uint64_t duration,bool live) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(!active_token_||ticket.token!=active_token_||ticket.slot!=active_slot_)return false;
 if(!complete)return false;
 auto& slot=slots_[active_slot_];active_token_=0;active_slot_=3;
 if(duration>20000)RecordBudgetPressureLocked();
 const bool publish=live&&copy_invalidation_==invalidation_&&Valid(info,capacity_)&&MatchesSourceGenerationLocked(info);
 // Recovery only counts current, completed live copies. An exact request,
 // cancellation or stale-generation drain cannot accelerate the live budget.
 if(duration<=20000&&publish&&!active_exact_&&recovery_copies_) {
  const auto remaining=recovery_copies_.load()-1;recovery_copies_=remaining;
  retry_after_us_=remaining?Now()+1000000:0;
 }
 if(publish){slot.info=info;slot.state=State::Published;latest_=ticket.slot;++published_;}
 else {slot.state=State::Free;if(latest_==ticket.slot)latest_=3;}
 return publish;
}
bool PreviewMailbox::CanAttemptLive() const noexcept {
 return !live_suspended_.load()&&(!recovery_copies_.load()||Now()>=retry_after_us_.load());
}
void PreviewMailbox::RecordBudgetPressureLocked() noexcept {
 // Retain cumulative evidence while transient scheduling/copy pressure recovers.
 // Three healthy one-Hz probes restore the caller's original live cadence.
 budget_exceeded_=true;++budget_events_;recovery_copies_=3;
 retry_after_us_=Now()+1000000;
}
void PreviewMailbox::RecordReadyDuration(uint64_t duration) {
 if(duration<=200000)return;
 std::lock_guard<std::mutex> lock(mutex_);RecordBudgetPressureLocked();
}
void PreviewMailbox::Invalidate() {
 std::lock_guard<std::mutex> lock(mutex_);InvalidateLocked();
}
void PreviewMailbox::InvalidateLocked() {
 ++invalidation_;latest_=3;
 for(auto& slot:slots_)if(slot.state!=State::Writing)slot.state=State::Free;
 snapshot_request_=0;snapshot_deadline_us_=0;snapshot_cancelled_=false;snapshot_candidate_=snapshot_confirmed_=false;
}
bool PreviewMailbox::AdmitSourceGeneration(uint64_t source,uint64_t generation,uint64_t epoch) {
 if(!source||!generation||!epoch)return false;
 std::lock_guard<std::mutex> lock(mutex_);
 if(source_id_==source&&source_epoch_==epoch&&generation<generation_)return false;
 if(source_id_&&(source_id_!=source||source_epoch_!=epoch||generation_!=generation))InvalidateLocked();
 source_id_=source;source_epoch_=epoch;generation_=generation;return true;
}
bool PreviewMailbox::MatchesSourceGenerationLocked(const HV_RuntimePreviewV1& info) const noexcept {
 return !source_id_||(source_id_==info.source_id&&generation_==info.generation&&source_epoch_==info.config_epoch);
}
bool PreviewMailbox::MatchesSourceGeneration(uint64_t source,uint64_t generation,uint64_t epoch) const {
 std::lock_guard<std::mutex> lock(mutex_);
 return source_id_==source&&generation_==generation&&source_epoch_==epoch;
}
bool PreviewMailbox::HasInFlight() const {std::lock_guard<std::mutex> lock(mutex_);return active_token_!=0;}
PreviewMailbox::ReadLease& PreviewMailbox::ReadLease::operator=(ReadLease&& other) noexcept {
 if(this==&other)return *this;Reset();owner_=std::exchange(other.owner_,nullptr);
 slot_=other.slot_;info_=other.info_;data_=other.data_;return *this;
}
void PreviewMailbox::ReadLease::Reset() noexcept {
 if(owner_){owner_->slots_[slot_].readers.fetch_sub(1,std::memory_order_release);owner_=nullptr;}data_=nullptr;
}
PreviewMailbox::ReadLease PreviewMailbox::AcquireLatest(uint64_t after) {
 ReadLease result;std::lock_guard<std::mutex> lock(mutex_);
 if(latest_>=3)return result;auto& slot=slots_[latest_];
 if(slot.state!=State::Published||slot.info.frame_id<=after)return result;
 slot.readers.fetch_add(1,std::memory_order_acquire);result.owner_=this;result.slot_=latest_;
 result.info_=slot.info;result.data_=slot.bytes.data();return result;
}
int PreviewMailbox::CopyLatest(uint64_t after,void* target,uint32_t capacity,HV_RuntimePreviewV1* out) {
 if(!out||out->struct_size<sizeof(*out)||out->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
 auto lease=AcquireLatest(after);if(!lease)return HV_CONTROL_NO_DATA;
 *out=lease.Info();if(!target||capacity<out->rgba_bytes)return HV_CONTROL_BUFFER_TOO_SMALL;
 std::memcpy(target,lease.Data(),out->rgba_bytes);return 0;
}
int PreviewMailbox::RequestSnapshot(uint64_t* out) {
 if(!out)return HV_ERR_INVALID_ARGUMENT;*out=0;std::lock_guard<std::mutex> lock(mutex_);
 const auto now=Now();if(now<0||now>std::numeric_limits<int64_t>::max()-5000000)return HV_ERR_INVALID_ARGUMENT;
 ExpireSnapshotLocked(now);
 if((snapshot_request_&&!snapshot_cancelled_&&snapshot_status_==HV_CONTROL_NO_DATA)||snapshot_writing_||snapshot_readers_)return HV_CONTROL_BUSY;
 snapshot_request_=next_snapshot.fetch_add(1);if(!snapshot_request_)snapshot_request_=next_snapshot.fetch_add(1);
 snapshot_deadline_us_=now+5000000;snapshot_cancelled_=false;
 snapshot_candidate_=snapshot_confirmed_=false;snapshot_status_=HV_CONTROL_NO_DATA;*out=snapshot_request_;return 0;
}
void PreviewMailbox::ExpireSnapshotLocked(int64_t now) {
 if(snapshot_request_&&!snapshot_confirmed_&&!snapshot_cancelled_&&snapshot_status_==HV_CONTROL_NO_DATA&&now>=snapshot_deadline_us_) {
  snapshot_status_=HV_CONTROL_TIMEOUT;snapshot_candidate_=false;
 }
}
void PreviewMailbox::ExpireSnapshot() {std::lock_guard<std::mutex> lock(mutex_);ExpireSnapshotLocked(Now());}
uint64_t PreviewMailbox::PendingSnapshot() {
 std::lock_guard<std::mutex> lock(mutex_);ExpireSnapshotLocked(Now());
 return snapshot_cancelled_||snapshot_confirmed_||snapshot_status_!=HV_CONTROL_NO_DATA?0:snapshot_request_;
}
bool PreviewMailbox::StoreSnapshotCandidate(uint64_t request,const HV_RuntimePreviewV1& info,const void* data,uint32_t bytes) {
 {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
  if(!lock)return false;ExpireSnapshotLocked(Now());
  if(!request||request!=snapshot_request_||snapshot_cancelled_||snapshot_status_!=HV_CONTROL_NO_DATA||snapshot_candidate_||snapshot_writing_||snapshot_readers_||
      !data||bytes<info.rgba_bytes||!Valid(info,static_cast<uint32_t>(snapshot_.size()))||!MatchesSourceGenerationLocked(info))return false;
  snapshot_writing_=true;}
 std::memcpy(snapshot_.data(),data,info.rgba_bytes);
 std::lock_guard<std::mutex> lock(mutex_);snapshot_writing_=false;
 ExpireSnapshotLocked(Now());
 if(request!=snapshot_request_||snapshot_cancelled_||snapshot_status_!=HV_CONTROL_NO_DATA||!MatchesSourceGenerationLocked(info))return false;
 snapshot_info_=info;snapshot_candidate_=true;return true;
}
bool PreviewMailbox::SnapshotCandidate(uint64_t& request,HV_RuntimePreviewV1& info) {
 std::lock_guard<std::mutex> lock(mutex_);ExpireSnapshotLocked(Now());
 if(!snapshot_request_||snapshot_cancelled_||snapshot_status_!=HV_CONTROL_NO_DATA||!snapshot_candidate_||snapshot_writing_||snapshot_confirmed_)return false;
 request=snapshot_request_;info=snapshot_info_;return true;
}
bool PreviewMailbox::ConfirmSnapshot(uint64_t request,uint64_t source,uint64_t generation,uint64_t frame,uint64_t epoch) {
 std::lock_guard<std::mutex> lock(mutex_);
 ExpireSnapshotLocked(Now());
 if(!request||request!=snapshot_request_||snapshot_cancelled_||snapshot_status_!=HV_CONTROL_NO_DATA||!snapshot_candidate_||snapshot_writing_||snapshot_info_.source_id!=source||
    snapshot_info_.generation!=generation||snapshot_info_.frame_id!=frame||snapshot_info_.config_epoch!=epoch||!MatchesSourceGenerationLocked(snapshot_info_))return false;
 snapshot_confirmed_=true;return true;
}
void PreviewMailbox::RejectSnapshotCandidate(uint64_t request) {
 std::lock_guard<std::mutex> lock(mutex_);if(request==snapshot_request_&&!snapshot_writing_&&!snapshot_readers_)
  snapshot_candidate_=snapshot_confirmed_=false;
}
int PreviewMailbox::CancelSnapshot(uint64_t request) {
 if(!request)return HV_ERR_INVALID_ARGUMENT;
 std::lock_guard<std::mutex> lock(mutex_);
 if(request!=snapshot_request_)return HV_CONTROL_NO_DATA;
 snapshot_cancelled_=true;snapshot_candidate_=snapshot_confirmed_=false;return 0;
}
void PreviewMailbox::FailSnapshot(uint64_t request,int status) {
 std::lock_guard<std::mutex> lock(mutex_);
 if(request==snapshot_request_){snapshot_status_=status;snapshot_candidate_=snapshot_confirmed_=false;}
}
int PreviewMailbox::CopySnapshot(uint64_t request,void* target,uint32_t capacity,HV_RuntimePreviewV1* out) {
 if(!out||out->struct_size<sizeof(*out)||out->api_version!=1||!request)return HV_ERR_INVALID_ARGUMENT;
 {std::lock_guard<std::mutex> lock(mutex_);
  ExpireSnapshotLocked(Now());
  if(request!=snapshot_request_||snapshot_cancelled_)return HV_CONTROL_NO_DATA;
  if(snapshot_status_!=HV_CONTROL_NO_DATA)return snapshot_status_;
  if(!snapshot_confirmed_||snapshot_writing_)return HV_CONTROL_NO_DATA;
  *out=snapshot_info_;if(!target||capacity<out->rgba_bytes)return HV_CONTROL_BUFFER_TOO_SMALL;
  snapshot_readers_.fetch_add(1);}
 std::memcpy(target,snapshot_.data(),out->rgba_bytes);
 {std::lock_guard<std::mutex> lock(mutex_);snapshot_readers_.fetch_sub(1);
  if(request==snapshot_request_){snapshot_request_=0;snapshot_candidate_=snapshot_confirmed_=false;}}
 return 0;
}
}
