#pragma once

#include "humanvision_plugin_v3.h"
#include <cstdint>
#if defined(__ANDROID__)
#include <command.h>
#include <net.h>
#include <memory>
#include <vector>
#endif

namespace humanvision::runtime::ncnn_backend {

// The one-job detector lease is independent of the three AHB import slots.
// A GPU completion proof is required before the source role may be released.
class PreparedInputState {
public:
    bool Initialize(uint64_t generation) noexcept;
    bool Begin(const HV_GpuFrameRefV1& frame, HV_GpuPreparedRefV1& ref) noexcept;
    bool ProveGpuCopy() noexcept;
    bool ReleaseSourceRole() noexcept;
    bool Ready(const HV_GpuPreparedRefV1& ref) const noexcept;
    bool Consume(const HV_GpuPreparedRefV1& ref) noexcept;
    bool Discard(const HV_GpuPreparedRefV1& ref) noexcept;
    void Quarantine() noexcept;
    bool Quarantined() const noexcept;
    bool Idle() const noexcept;
    bool RequiresProcessLifetimeRetention(bool terminal_gpu_fault) const noexcept;
private:
    enum class Stage : uint8_t { Idle, Copying, Proved, Detached, Quarantined };
    Stage stage_ = Stage::Idle;
    uint64_t generation_ = 0;
    uint64_t next_token_ = 1;
    HV_GpuPreparedRefV1 active_{};
};

#if defined(__ANDROID__)
struct InputContract;
// This cache has no AHB owner. Source import images and semaphores remain in
// their three generation slots; only the small detached tensor is retained.
struct PreparedGpuResources {
    std::unique_ptr<ncnn::VkCompute> compute;
    ncnn::VkMat normalized;
    ncnn::VkMat tensor;
    std::unique_ptr<ncnn::Extractor> extractor;
    std::unique_ptr<ncnn::Extractor> pristine_extractor;
    std::vector<ncnn::VkMat> gpu_outputs, fp32_outputs;
    std::vector<ncnn::Mat> cpu_outputs;
    std::vector<std::vector<float>> dense_outputs;
    uint64_t output_download_calls = 0; // Two bounded small outputs per detector job.
    bool Initialize(const ncnn::VulkanDevice* device, const ncnn::Net& net,
                    ncnn::VkAllocator* blob, ncnn::VkAllocator* staging,
                    const InputContract& contract);
    ~PreparedGpuResources();
};
#endif

}
