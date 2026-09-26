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
    std::atomic<uint32_t> width_{0}, height_{0};
};

class GpuRuntimeHost {
public:
    explicit GpuRuntimeHost(GpuConsumerSource& source) : source_(source) {}
    ~GpuRuntimeHost() { Stop(); }
    bool Start(std::shared_ptr<const GpuPluginModuleV3>, const HV_HostServicesV3&,
               const HV_PipelineConfigV1&, std::string& error);
    bool CopyLatest(HV_ObservationFrameV1&, int64_t& revision) const;
    std::string LastError() const;
    PipelineDiagnostics Diagnostics() const;
    void SetRevision(int64_t revision) noexcept { revision_.store(revision); }
    void Stop();
private:
    void Run();
    GpuConsumerSource& source_;
    std::shared_ptr<const GpuPluginModuleV3> module_;
    void* instance_ = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> revision_{0};
    mutable std::mutex result_mutex_;
    HV_ObservationFrameV1 latest_{};
    int64_t result_revision_ = 0, sequence_ = 0;
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
    gpu::BridgeResult Prepare(void* owner,const HV_AndroidGpuSubmissionV1&,void**,
                              PrepareFn,DimensionsFn) noexcept;
private:
    mutable std::mutex mutex_;
    void* owner_=nullptr;
    EndFn end_=nullptr;
    ActiveFn active_=nullptr;
};
}
