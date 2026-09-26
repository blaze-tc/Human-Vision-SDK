#pragma once
#include "host/runtime_host.h"
#include "host/profile_manager.h"
#include "host/backend_factory.h"
#include "host/gpu_runtime_host.h"
#include "services/body_services.h"
#include "core/stats_collector.h"

namespace humanvision::runtime {
// The composition root registers built-ins; the generic Host never names them.
class RuntimeSession {
public:
 ~RuntimeSession();
 bool Start(const std::filesystem::path& root,const std::string& profile,int capacity,std::string& error,
            HV_QueryPluginV3Fn gpu_pipeline_query=nullptr,GpuConsumerSource* gpu_test_source=nullptr);
 bool Submit(const HV_VideoFrame&,std::string& error);
 bool SetRegions(const HV_Rect*,uint32_t count,int64_t revision,std::string& error);
 BodySnapshot Copy(int64_t sample_time,HV_RuntimeStatsV1& stats);
 HV_RuntimeStatsV2 StatsV2();
 void RecordSourceArrival(bool rate_limited) noexcept;
 void SetCaptureProvenance(uint32_t provenance) noexcept { capture_provenance_.store(provenance,std::memory_order_relaxed); }
 void RecordGpuCaptureAttempt() noexcept { gpu_capture_requested_.fetch_add(1,std::memory_order_relaxed); }
 void RecordGpuNoSlotDrop() noexcept { gpu_no_slot_drops_.fetch_add(1,std::memory_order_relaxed); }
 std::string LastError() const;
 // Additive platform APIs report through the same runtime error channel.
 void ReportError(const char* error) {std::lock_guard<std::mutex> lock(mutex_);error_=error;}
 std::string Diagnostics() const;
 void RecordGpuDimensions(uint32_t width,uint32_t height) noexcept;
 void SetGpuSourceLeaseActive(bool active) noexcept;
 bool UsesGpuRoute() const noexcept {return profile_&&profile_->gpu_route;}
private:
 void Run();
 void Poll(); // mutex_ held
 bool AcceptGpuObservation(const HV_GpuObservationFrameV3&,int64_t revision,int64_t capture_steady_us);
 PluginRegistry registry_;
 std::shared_ptr<const RuntimeProfile> profile_;
 std::unique_ptr<BackendFactory> factory_;
 std::unique_ptr<BridgeGpuConsumerSource> bridge_source_;
 std::unique_ptr<GpuRuntimeHost> gpu_;
 RuntimeHost body_,hand_;
 LatestFrameSlot input_;
 std::thread worker_;
 mutable std::mutex mutex_;
 BodyServices services_;
 std::array<HV_Rect,8> regions_{};
 uint32_t region_count_=0;
 int64_t revision_=0,body_sequence_=0,hand_sequence_=0,last_body_submit_=0;
 HV_RuntimeStatsV1 stats_{};
 humanvision::StatsCollectorV2 stats_v2_;
 std::atomic<uint64_t> source_frames_seen_{0}, source_rate_limited_drops_{0};
 std::atomic<uint32_t> capture_provenance_{HV_CAPTURE_PROVENANCE_UNKNOWN};
 std::atomic<uint64_t> gpu_capture_requested_{0};
 std::atomic<uint64_t> gpu_no_slot_drops_{0};
 int64_t last_hand_result_time_=0;
 std::string error_;
};
}
