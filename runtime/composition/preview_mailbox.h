#pragma once
#include "humanvision/humanvision_runtime_control.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace humanvision::runtime {
struct PreviewAllocationPolicy {
 void* context=nullptr;
 void (*allocate)(void*,std::vector<uint8_t>&,uint32_t)=nullptr;
};
struct PreviewClockSource {
 void* context=nullptr;
 int64_t (*now_us)(void*) noexcept=nullptr;
};
// Native-owned immutable copies. No reader retains a decoder or analysis lease.
class PreviewMailbox {
public:
 using AllocationPolicy=PreviewAllocationPolicy;
 using ClockSource=PreviewClockSource;
 enum class ConfigureResult {Ready,Busy,Invalid,AllocationFailure,BudgetExceeded};
 explicit PreviewMailbox(AllocationPolicy allocation={},ClockSource clock={}) noexcept;
 struct CopyTicket {uint64_t token=0;uint32_t slot=3;uint8_t* data=nullptr;uint32_t capacity=0;};
 class ReadLease {
 public:
  ReadLease()=default;~ReadLease(){Reset();}
  ReadLease(const ReadLease&)=delete;ReadLease& operator=(const ReadLease&)=delete;
  ReadLease(ReadLease&& other) noexcept {*this=std::move(other);}
  ReadLease& operator=(ReadLease&& other) noexcept;
  explicit operator bool() const noexcept {return owner_!=nullptr;}
  const HV_RuntimePreviewV1& Info() const noexcept {return info_;}
  const uint8_t* Data() const noexcept {return data_;}
  void Reset() noexcept;
 private:
  friend class PreviewMailbox;
  PreviewMailbox* owner_=nullptr;uint32_t slot_=3;
  HV_RuntimePreviewV1 info_{};const uint8_t* data_=nullptr;
 };
 bool Configure(uint32_t max_bytes);
 ConfigureResult ConfigureDetailed(uint32_t max_bytes) noexcept;
 ConfigureResult ConfigureCandidate(uint32_t max_bytes) noexcept;
 static constexpr uint32_t kCandidateMaximumBytes=1920u*1080u*4u;
 bool TryBeginCopy(uint32_t free_source_credits,bool additional_source_lease,
                   bool analysis_pending,CopyTicket&,bool exact=false);
 // false completion retains the reservation, including after cancellation.
 bool FinishCopy(const CopyTicket&,const HV_RuntimePreviewV1&,bool actual_complete,
                 uint64_t copy_duration_us,bool publish_live=true);
 void Invalidate();
 bool AdmitSourceGeneration(uint64_t source,uint64_t generation,uint64_t epoch);
 bool MatchesSourceGeneration(uint64_t source,uint64_t generation,uint64_t epoch) const;
 bool HasInFlight() const;
 bool BudgetExceeded() const noexcept {return budget_exceeded_.load();}
 void SuspendLive() noexcept {budget_exceeded_=true;}
 static constexpr uint32_t BufferCount() noexcept {return 3;}
 ReadLease AcquireLatest(uint64_t after_frame);
 int CopyLatest(uint64_t after_frame,void*,uint32_t,HV_RuntimePreviewV1*);
 int RequestSnapshot(uint64_t*);
 uint64_t PendingSnapshot();
 bool StoreSnapshotCandidate(uint64_t,const HV_RuntimePreviewV1&,const void*,uint32_t);
 bool SnapshotCandidate(uint64_t&,HV_RuntimePreviewV1&);
 bool ConfirmSnapshot(uint64_t,uint64_t source,uint64_t generation,uint64_t frame,uint64_t epoch);
 void RejectSnapshotCandidate(uint64_t);
 int CancelSnapshot(uint64_t);
 void FailSnapshot(uint64_t,int status);
 void ExpireSnapshot();
 int CopySnapshot(uint64_t,void*,uint32_t,HV_RuntimePreviewV1*);
 uint64_t Published() const noexcept {return published_.load();}
 uint64_t Skipped() const noexcept {return skipped_.load();}
private:
 enum class State {Free,Writing,Published};
 struct Slot {std::vector<uint8_t> bytes;HV_RuntimePreviewV1 info{};State state=State::Free;std::atomic<uint32_t> readers{0};};
 static bool Valid(const HV_RuntimePreviewV1&,uint32_t) noexcept;
 bool MatchesSourceGenerationLocked(const HV_RuntimePreviewV1&) const noexcept;
 void InvalidateLocked();
 void ExpireSnapshotLocked(int64_t now);
 int64_t Now() const noexcept;
 AllocationPolicy allocation_;
 ClockSource clock_;
 mutable std::mutex mutex_;
 std::array<Slot,3> slots_;
 uint32_t latest_=3,active_slot_=3,capacity_=0;
 bool allocation_pending_=false;
 uint64_t capacity_epoch_=0;
 uint64_t next_copy_=0,active_token_=0,invalidation_=0,copy_invalidation_=0;
 uint64_t source_id_=0,generation_=0,source_epoch_=0;
 std::atomic<bool> budget_exceeded_{false};
 std::atomic<uint64_t> published_{0},skipped_{0};
 std::vector<uint8_t> snapshot_;
 HV_RuntimePreviewV1 snapshot_info_{};
 uint64_t snapshot_request_=0;
 int64_t snapshot_deadline_us_=0;
 bool snapshot_cancelled_=false;
 bool snapshot_candidate_=false,snapshot_confirmed_=false,snapshot_writing_=false;
 int snapshot_status_=HV_CONTROL_NO_DATA;
 std::atomic<uint32_t> snapshot_readers_{0};
};
}
