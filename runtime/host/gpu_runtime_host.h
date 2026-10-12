#pragma once
#include "host/backend_factory.h"
#include "common/pipeline_diagnostics.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace humanvision::runtime {
class GpuConsumerSource {
public:
    virtual ~GpuConsumerSource() = default;
    virtual void SetConcurrency(uint32_t) noexcept {}
    virtual gpu::SlotResult Claim(gpu::ConsumerFrame&) noexcept = 0;
    virtual gpu::SlotResult ClaimDropped(gpu::ConsumerFrame&) noexcept { return gpu::SlotResult::NoReady; }
    virtual gpu::SlotResult Retire(gpu::ConsumerFrame&) noexcept = 0;
    virtual void Quarantine(gpu::ConsumerFrame&) noexcept = 0;
    virtual bool DrainUnsubmitted(gpu::ConsumerFrame&) noexcept = 0;
    virtual uint64_t Generation() const noexcept = 0;
    virtual uint32_t Width() const noexcept = 0;
    virtual uint32_t Height() const noexcept = 0;
};

class BridgeGpuConsumerSource final : public GpuConsumerSource {
public:
    explicit BridgeGpuConsumerSource(gpu::UnityVulkanBridge* bridge) : bridge_(bridge) {}
    void SetConcurrency(uint32_t count) noexcept override { concurrency_=count; }
    void SetDimensions(uint32_t width, uint32_t height) noexcept { width_=width; height_=height; }
    void SetLeaseActive(bool active) noexcept { lease_active_.store(active,std::memory_order_release); }
    gpu::SlotResult Claim(gpu::ConsumerFrame&) noexcept override;
    gpu::SlotResult ClaimDropped(gpu::ConsumerFrame&) noexcept override;
    gpu::SlotResult Retire(gpu::ConsumerFrame&) noexcept override;
    void Quarantine(gpu::ConsumerFrame&) noexcept override;
    bool DrainUnsubmitted(gpu::ConsumerFrame&) noexcept override;
    uint64_t Generation() const noexcept override;
    uint32_t Width() const noexcept override { return width_.load(); }
    uint32_t Height() const noexcept override { return height_.load(); }
private:
    gpu::UnityVulkanBridge* bridge_ = nullptr; // borrowed for the source lease
    std::atomic<bool> lease_active_{false};
    std::atomic<uint32_t> concurrency_{1};
    std::atomic<uint32_t> width_{0}, height_{0};
};

class GpuRuntimeHost {
public:
    using ObservationSink = bool (*)(void*,const HV_GpuObservationFrameV3&,int64_t,int64_t);
    explicit GpuRuntimeHost(GpuConsumerSource& source) : source_(source) {}
    ~GpuRuntimeHost() { Stop(); }
    bool Start(std::shared_ptr<const GpuPluginModuleV3>, const HV_HostServicesV3&,
               const HV_PipelineConfigV1&, std::string& error);
    bool StartPool(std::shared_ptr<const GpuPluginModuleV3>, const HV_HostServicesV3&,
                   const HV_PipelineConfigV1&, uint32_t count, std::string& error);
    bool CopyLatest(HV_ObservationFrameV1&, int64_t& revision) const;
    bool CopyLatest(HV_GpuObservationFrameV3&, int64_t& revision) const;
    bool CopyLatest(HV_GpuObservationFrameV3&, int64_t& revision, int64_t& capture_steady_us) const;
    std::string LastError() const;
    PipelineDiagnostics Diagnostics() const;
    uint64_t SupersededReadyDrops() const noexcept { return superseded_ready_drops_.load(); }
    uint64_t PoseJobDrops() const noexcept { return pose_job_drops_.load(); }
    uint64_t ImportErrors() const noexcept { return import_errors_.load(); }
    void SetObservationSink(void* context,ObservationSink sink) noexcept { sink_context_=context;sink_=sink; }
    void SetRevision(int64_t revision) noexcept { revision_.store(revision); }
    // Busy means executing the pipeline call (includes its waits), not hardware utilization.
    uint32_t WorkerCount() const noexcept { return worker_count_.load(); }
    uint32_t ActiveWorkers() const noexcept { return active_workers_.load(); }
    void Stop();
private:
    void Run(uint32_t index);
    GpuConsumerSource& source_;
    std::shared_ptr<const GpuPluginModuleV3> module_;
    std::array<void*,3> instances_{};
    std::array<std::thread,3> workers_{};
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> worker_count_{0},active_workers_{0};
    std::mutex publication_mutex_,source_claim_mutex_;
    uint64_t latest_generation_=0;
    std::atomic<int64_t> revision_{0};
    std::atomic<uint64_t> superseded_ready_drops_{0}, pose_job_drops_{0};
    std::atomic<uint64_t> import_errors_{0};
    void* sink_context_=nullptr;
    ObservationSink sink_=nullptr;
    mutable std::mutex result_mutex_;
    HV_GpuObservationFrameV3 latest_{};
    int64_t result_revision_ = 0, sequence_ = 0;
    int64_t capture_steady_us_ = 0;
    bool has_result_ = false;
    int capacity_ = 0;
    std::string error_;
};

// Unity's producer is process-global. Only one runtime handle may own its
// source lease; the control callbacks run under this coordinator's mutex.
class GpuSourceLeaseCoordinator {
public:
    using BeginFn = bool (*)(void*) noexcept;
    using EndFn = void (*)() noexcept;
    using ActiveFn = void (*)(void*,bool) noexcept;
    using PrepareFn = gpu::BridgeResult (*)(const HV_AndroidGpuSubmissionV1&,void**) noexcept;
    using DimensionsFn = void (*)(void*,uint32_t,uint32_t) noexcept;
    static GpuSourceLeaseCoordinator& Instance() noexcept;
    bool Begin(void* owner,void* texture,BeginFn,EndFn,ActiveFn,std::string& error);
    bool End(void* owner,std::string& error);
    bool Owns(void* owner) const noexcept;
    using RetireFn = gpu::BridgeResult (*)(uint64_t, HV_AndroidGpuSourceRetirementV2&) noexcept;
    gpu::BridgeResult Retire(void* owner, HV_AndroidGpuSourceRetirementV2&, RetireFn) noexcept;
    bool CompleteRetirement(const HV_AndroidGpuSourceRetirementV2&) noexcept;
    gpu::BridgeResult Prepare(void* owner,const HV_AndroidGpuSubmissionV1&,void**,
                              PrepareFn,DimensionsFn) noexcept;
    using PrepareCopyFn = gpu::BridgeResult (*)(const HV_AndroidGpuSubmissionV1&, uint64_t,
        uint64_t, void**, HV_AndroidGpuFrameCopyTicketV2&) noexcept;
    gpu::BridgeResult PrepareCopy(void*, const HV_AndroidGpuSubmissionV1&, uint64_t, uint64_t,
        void**, HV_AndroidGpuFrameCopyTicketV2&, PrepareCopyFn, DimensionsFn) noexcept;
private:
    mutable std::mutex mutex_;
    void* owner_=nullptr;
    EndFn end_=nullptr;
    ActiveFn active_=nullptr;
    uint64_t retirement_generation_=0;
};
}
