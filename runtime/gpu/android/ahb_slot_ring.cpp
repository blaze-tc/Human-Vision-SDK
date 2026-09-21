#include "gpu/android/ahb_slot_ring.h"
#include <utility>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace humanvision::gpu {
namespace {
void CloseNativeFd(void*, int fd) noexcept {
#if defined(_WIN32)
    (void)_close(fd);
#else
    (void)close(fd);
#endif
}
bool ValidState(AhbSlotState s) noexcept { return s < AhbSlotState::Count; }
}
SyncFd::SyncFd(int fd) noexcept : SyncFd(fd, CloseNativeFd, nullptr) {}
SyncFd::SyncFd(int fd, Closer closer, void* context) noexcept
    : fd_(fd), closer_(closer ? closer : CloseNativeFd), context_(context) {}
SyncFd::~SyncFd() { Reset(); }
SyncFd::SyncFd(SyncFd&& other) noexcept
    : fd_(other.Release()), closer_(other.closer_), context_(other.context_) {}
SyncFd& SyncFd::operator=(SyncFd&& other) noexcept {
    if(this != &other) { Reset(); closer_=other.closer_;context_=other.context_;fd_=other.Release(); }
    return *this;
}
int SyncFd::Release() noexcept { return std::exchange(fd_, -1); }
void SyncFd::Reset() noexcept { const int fd=Release();if(fd>=0)closer_(context_,fd); }

OwnedSlotResource::OwnedSlotResource(uintptr_t handle, Deleter deleter, void* context) noexcept
    : handle_(handle), deleter_(deleter), context_(context) {}
OwnedSlotResource::~OwnedSlotResource() { Reset(); }
OwnedSlotResource::OwnedSlotResource(OwnedSlotResource&& other) noexcept
    : handle_(std::exchange(other.handle_,0)),deleter_(other.deleter_),context_(other.context_) {}
OwnedSlotResource& OwnedSlotResource::operator=(OwnedSlotResource&& other) noexcept {
    if(this!=&other){ Reset();handle_=std::exchange(other.handle_,0);deleter_=other.deleter_;context_=other.context_; }
    return *this;
}
void OwnedSlotResource::Reset() noexcept {
    const auto handle=std::exchange(handle_,0);if(handle&&deleter_)deleter_(context_,handle);
}
SlotResources::~SlotResources() { Reset(); }
void SlotResources::Reset() noexcept {
    // GPU completion is already proven by the owner before teardown. Destroy
    // objects referring to allocators/memory before their backing owners.
    ncnn_import_pipeline.Reset();ncnn_preprocessing_buffers.Reset();ncnn_image_mat.Reset();
    ncnn_allocator.Reset();ncnn_ownership_command.Reset();ncnn_command_pool.Reset();
    ncnn_import_semaphore.Reset();ncnn_completion.Reset();
    unity_command_buffer.Reset();unity_command_pool.Reset();unity_fence.Reset();unity_export_semaphore.Reset();
    unity_framebuffer.Reset();unity_view.Reset();unity_image.Reset();unity_memory.Reset();
    ncnn_ahb_lease.Reset();unity_ahb_lease.Reset();ahb_allocation.Reset();
}
bool SlotContract::operator==(const SlotContract& c) const noexcept {
    return width==c.width&&height==c.height&&actual_format==c.actual_format&&actual_usage==c.actual_usage&&
        rotation==c.rotation&&mirror==c.mirror&&camera_session==c.camera_session&&input_contract_hash==c.input_contract_hash;
}
AhbSlotRing::AhbSlotRing(SlotLifecycle lifecycle) noexcept : lifecycle_(lifecycle) {}
AhbSlotRing::~AhbSlotRing() { Shutdown(); }
bool AhbSlotRing::IsLegalTransition(AhbSlotState from, AhbSlotState to, CompletionProof proof) noexcept {
    using S=AhbSlotState;
    if(!ValidState(from)||!ValidState(to)||from==to)return false;
    if(to==S::DropDrain&&from!=S::Free&&from!=S::DropDrain)
        return from==S::ReadyForNcnn||proof==CompletionProof::GpuQuiescent;
    switch(from){
    case S::Free:return to==S::EventReserved;
    case S::EventReserved:return to==S::UnityCopySubmitted;
    case S::UnityCopySubmitted:return to==S::ProducerSignalPending;
    case S::ProducerSignalPending:return to==S::ReadyForNcnn;
    case S::ReadyForNcnn:return to==S::InferenceRunning;
    case S::InferenceRunning:return to==S::ConsumerReleasePending;
    case S::ConsumerReleasePending:case S::DropDrain:
        return to==S::Free&&proof==CompletionProof::GpuQuiescent;
    default:return false;
    }
}
bool AhbSlotRing::Matches(const SlotToken& token) const noexcept {
    if(token.index>=kSlotCount||token.generation!=generation_.load(std::memory_order_acquire))return false;
    const auto& slot=slots_[token.index];
    return slot.state.load(std::memory_order_acquire)!=AhbSlotState::Free&&
        slot.metadata.generation==token.generation&&slot.metadata.frame_id==token.frame_id;
}
void AhbSlotRing::RecycleLocked(Slot& slot) noexcept {
    slot.producer_fd.Reset();slot.metadata={};
    auto state=slot.state.load(std::memory_order_acquire);
    slot.state.compare_exchange_strong(state,AhbSlotState::Free,std::memory_order_acq_rel);
}
void AhbSlotRing::DrainLocked() noexcept {
    for(uint32_t i=0;i<kSlotCount;++i){auto& slot=slots_[i];auto state=slot.state.load(std::memory_order_acquire);
        // Even Free slots may own cached device objects. The lifecycle hook
        // also quiesces/cancels outstanding external events before destruction.
        lifecycle_.drain(lifecycle_.context,i,state,slot.resources,slot.producer_fd);
        if(state==AhbSlotState::ReadyForNcnn)generation_drops_.fetch_add(1,std::memory_order_relaxed);
        if(state!=AhbSlotState::Free){
            if(state!=AhbSlotState::DropDrain)
                slot.state.compare_exchange_strong(state,AhbSlotState::DropDrain,std::memory_order_acq_rel);
            RecycleLocked(slot);
        }
        slot.producer_fd.Reset();slot.resources.Reset();
    }
    initialized_=false;
}
bool AhbSlotRing::Reconfigure(const SlotContract& contract,CallContext context) {
    if(context!=CallContext::Control||!lifecycle_.create||!lifecycle_.drain||
       !contract.width||!contract.height||!contract.actual_format)return false;
    // Serialize control operations before closing admission, so a concurrent
    // rebuild cannot reopen admission during another control operation's drain.
    std::lock_guard<std::mutex> control_lock(lifecycle_mutex_);
    accepting_.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    if(initialized_&&contract==contract_){accepting_.store(true,std::memory_order_release);return true;}
    if(initialized_)DrainLocked();
    // Old token admission was closed before drain. Publish a new generation only
    // after the previous one finished; unsuccessful construction consumes it too.
    generation_.fetch_add(1,std::memory_order_acq_rel);
    last_reserved_frame_=last_claimed_frame_=0;contract_=contract;
    for(uint32_t i=0;i<kSlotCount;++i){
        if(!lifecycle_.create(lifecycle_.context,i,contract_,slots_[i].resources)){
            // Factories never submit GPU work. Roll back partial construction in
            // dependency order without invoking a GPU completion hook.
            for(auto& slot:slots_)slot.resources.Reset();
            return false;
        }
    }
    initialized_=true;accepting_.store(true,std::memory_order_release);return true;
}
bool AhbSlotRing::Shutdown(CallContext context) {
    if(context!=CallContext::Control)return false;
    std::lock_guard<std::mutex> control_lock(lifecycle_mutex_);
    accepting_.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> lock(mutex_);
    if(initialized_)DrainLocked();
    return true;
}
SlotResult AhbSlotRing::Reserve(uint64_t frame,int64_t timestamp,SlotToken& token) {
    token={};if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    if(!frame||frame<=last_reserved_frame_||timestamp<0)return SlotResult::Invalid;
    for(uint32_t i=0;i<kSlotCount;++i){auto& slot=slots_[i];auto expected=AhbSlotState::Free;
        if(!slot.state.compare_exchange_strong(expected,AhbSlotState::EventReserved,std::memory_order_acq_rel))continue;
        const auto gen=generation_.load(std::memory_order_acquire);
        slot.metadata={gen,frame,timestamp};last_reserved_frame_=frame;token={i,gen,frame};return SlotResult::Ok;
    }
    no_slot_drops_.fetch_add(1,std::memory_order_relaxed);return SlotResult::NoSlot;
}
SlotResult AhbSlotRing::Transition(const SlotToken& token,AhbSlotState from,AhbSlotState to,CompletionProof proof) {
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    // Reservation, metadata/fd publication and latest selection have dedicated
    // operations. General transitions cannot bypass their admission policies.
    if(!Matches(token)||!IsLegalTransition(from,to,proof)||from==AhbSlotState::Free||
        to==AhbSlotState::ReadyForNcnn||to==AhbSlotState::InferenceRunning)return SlotResult::Invalid;
    auto& slot=slots_[token.index];
    if(slot.state.load(std::memory_order_acquire)!=from)return SlotResult::Invalid;
    if(to==AhbSlotState::Free){RecycleLocked(slot);return SlotResult::Ok;}
    if(!slot.state.compare_exchange_strong(from,to,std::memory_order_acq_rel))return SlotResult::Invalid;
    if(from==AhbSlotState::ReadyForNcnn&&to==AhbSlotState::DropDrain)
        generation_drops_.fetch_add(1,std::memory_order_relaxed);
    return SlotResult::Ok;
}
SlotResult AhbSlotRing::PublishReady(const SlotToken& token,SyncFd& fd) {
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    if(!Matches(token)||fd.Get()<0)return SlotResult::Invalid;
    auto& slot=slots_[token.index];auto expected=AhbSlotState::ProducerSignalPending;
    if(slot.state.load(std::memory_order_acquire)!=expected)return SlotResult::Invalid;
    slot.producer_fd=std::move(fd);
    // Worker acquire observes the complete metadata and its sole fd owner.
    slot.state.compare_exchange_strong(expected,AhbSlotState::ReadyForNcnn,std::memory_order_acq_rel);
    return SlotResult::Ok;
}
SlotResult AhbSlotRing::ClaimNewest(SlotToken& token,SlotMetadata& metadata) {
    token={};metadata={};if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    uint32_t best=kSlotCount;const auto gen=generation_.load(std::memory_order_acquire);
    for(uint32_t i=0;i<kSlotCount;++i){const auto& slot=slots_[i];
        if(slot.state.load(std::memory_order_acquire)!=AhbSlotState::ReadyForNcnn)continue;
        if(slot.metadata.generation==gen&&slot.metadata.frame_id>last_claimed_frame_&&
           (best==kSlotCount||slot.metadata.frame_id>slots_[best].metadata.frame_id))best=i;
    }
    for(uint32_t i=0;i<kSlotCount;++i){if(i==best)continue;auto& slot=slots_[i];auto expected=AhbSlotState::ReadyForNcnn;
        if(slot.state.compare_exchange_strong(expected,AhbSlotState::DropDrain,std::memory_order_acq_rel))
            generation_drops_.fetch_add(1,std::memory_order_relaxed);
    }
    if(best==kSlotCount)return SlotResult::NoReady;
    auto& slot=slots_[best];auto expected=AhbSlotState::ReadyForNcnn;
    slot.state.compare_exchange_strong(expected,AhbSlotState::InferenceRunning,std::memory_order_acq_rel);
    metadata=slot.metadata;last_claimed_frame_=metadata.frame_id;token={best,metadata.generation,metadata.frame_id};return SlotResult::Ok;
}
SlotResult AhbSlotRing::TakeProducerFence(const SlotToken& token,SyncFd& fd) {
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(!accepting_.load(std::memory_order_acquire))return SlotResult::Closed;
    if(!Matches(token)||fd.Get()>=0)return SlotResult::Invalid;
    auto& slot=slots_[token.index];const auto state=slot.state.load(std::memory_order_acquire);
    if((state!=AhbSlotState::InferenceRunning&&state!=AhbSlotState::DropDrain)||slot.producer_fd.Get()<0)return SlotResult::Invalid;
    fd=std::move(slot.producer_fd);return SlotResult::Ok;
}
SlotResult AhbSlotRing::Inspect(uint32_t index,SlotSnapshot& snapshot) const {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return SlotResult::Busy;
    if(index>=kSlotCount)return SlotResult::Invalid;
    const auto& slot=slots_[index];snapshot={slot.state.load(std::memory_order_acquire),slot.metadata};return SlotResult::Ok;
}
SlotCounters AhbSlotRing::Counters() const noexcept {
    return {no_slot_drops_.load(std::memory_order_relaxed),generation_drops_.load(std::memory_order_relaxed)};
}
}
