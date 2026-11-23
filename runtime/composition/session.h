#pragma once
#include "common/frame_rate_credit.h"
#include "host/runtime_host.h"
#include "host/profile_manager.h"
#include "host/backend_factory.h"
#include "host/gpu_runtime_host.h"
#include "services/body_services.h"
#include "core/stats_collector.h"
#include "common/observation_journal.h"

namespace humanvision::runtime {
namespace detail {
// Shared internal configuration boundary for the composition root.
HV_PipelineConfigV1 BuildGpuPipelineConfig(const RuntimeProfile&,const char* asset_root);
HV_PipelineConfigV1 BuildPipelineConfig(const PipelineSelection&,int public_capacity,
                                      const char* asset_root,const char* options);
}
// The composition root registers built-ins; the generic Host never names them.
class RuntimeSession {
public:
 explicit RuntimeSession(std::shared_ptr<ObservationJournal> journal={})
  :journal_(journal?std::move(journal):std::make_shared<ObservationJournal>()){}
 ~RuntimeSession();
 // Serialized control-owner operation. Joins native owners; never call per frame.
 void Stop();
 // Control owner, before Start only. Legacy/compatible sessions never opt in.
 bool EnablePreparedImages(bool enabled) noexcept {
  if(profile_||closing_.load(std::memory_order_relaxed))return false;
  prepared_images_enabled_=enabled;return true;
 }
 bool Start(const std::filesystem::path& root,const std::string& profile,int capacity,std::string& error,
            HV_QueryPluginV3Fn gpu_pipeline_query=nullptr,GpuConsumerSource* gpu_test_source=nullptr);
 bool Submit(const HV_VideoFrame&,std::string& error);
 bool Submit(const HV_VideoFrame&,const FrameEnvelope&,std::string& error);
 bool SetRegions(const HV_Rect*,uint32_t count,int64_t revision,std::string& error);
 BodySnapshot Copy(int64_t sample_time,HV_RuntimeStatsV1& stats);
 HV_RuntimeStatsV2 StatsV2();
 ObservationRead ReadObservations(uint64_t cursor,ObservationEntry* output,uint32_t capacity) const {
  return journal_->ReadAfter(cursor,output,capacity);
 }
 void RecordSourceArrival(bool rate_limited) noexcept;
 void SetCaptureProvenance(uint32_t provenance) noexcept { capture_provenance_.store(provenance,std::memory_order_relaxed); }
 void RecordGpuCaptureAttempt() noexcept { gpu_capture_requested_.fetch_add(1,std::memory_order_relaxed); }
 void RecordGpuNoSlotDrop() noexcept { gpu_no_slot_drops_.fetch_add(1,std::memory_order_relaxed); }
 std::string LastError() const;
 // Additive platform APIs report through the same runtime error channel.
 void ReportError(const char* error) {std::lock_guard<std::mutex> lock(mutex_);error_=error;++runtime_errors_;}
 std::string Diagnostics() const;
 std::string ActualBackend() const {std::lock_guard<std::mutex> lock(mutex_);return factory_?factory_->SelectionDiagnostics().actual:std::string{};}
 uint64_t ErrorCount() const noexcept {return runtime_errors_.load(std::memory_order_relaxed)+body_.ErrorCount()+hand_.ErrorCount()+(gpu_?gpu_->ErrorCount():0);}
 void RecordGpuDimensions(uint32_t width,uint32_t height) noexcept;
 void SetGpuSourceLeaseActive(bool active) noexcept;
 bool UsesGpuRoute() const noexcept {return profile_&&profile_->gpu_route;}
private:
 void Run();
 void Notify() noexcept;
 static void CompletionReady(void* context) {static_cast<RuntimeSession*>(context)->Notify();}
 void DrainCompletions();
 void AcceptBodyObservation(const RuntimeHost::Completion&); // mutex_ held, coordinator only
 void AcceptHandObservation(const RuntimeHost::Completion&); // mutex_ held, coordinator only
 bool AcceptGpuObservation(const GpuRuntimeHost::Completion&); // mutex_ held, coordinator only
 void PublishObservation(ObservationKind,const FrameEnvelope&); // mutex_ held
 PluginRegistry registry_;
 std::shared_ptr<const RuntimeProfile> profile_;
 std::unique_ptr<BackendFactory> factory_;
 std::unique_ptr<BridgeGpuConsumerSource> bridge_source_;
 std::unique_ptr<GpuRuntimeHost> gpu_;
 RuntimeHost body_,hand_;
 LatestFrameSlotWithMetadata<FrameEnvelope> input_;
 std::thread worker_;
 std::mutex event_mutex_;
 std::condition_variable event_ready_;
 uint64_t notification_sequence_=0;
 std::atomic<bool> closing_{false},coordinator_stopped_{false};
 mutable std::mutex mutex_;
 BodyServices services_;
 std::array<HV_Rect,8> regions_{};
 uint32_t region_count_=0;
 int64_t revision_=0,body_sequence_=0,hand_sequence_=0;
 uint64_t gpu_completion_rejections_=0;
 bool regions_pending_=false;
 bool prepared_images_enabled_=false;
 FrameEnvelope active_envelope_;
 std::shared_ptr<ObservationJournal> journal_;
 FrameRateCredit body_admission_;
 HV_RuntimeStatsV1 stats_{};
 humanvision::StatsCollectorV2 stats_v2_;
 std::atomic<uint64_t> source_frames_seen_{0}, source_rate_limited_drops_{0};
 std::atomic<uint32_t> capture_provenance_{HV_CAPTURE_PROVENANCE_UNKNOWN};
 std::atomic<uint64_t> gpu_capture_requested_{0};
 std::atomic<uint64_t> gpu_no_slot_drops_{0};
 std::atomic<uint64_t> runtime_errors_{0};
 int64_t last_hand_result_time_=0;
 std::string error_,pool_fallback_;
};
}
