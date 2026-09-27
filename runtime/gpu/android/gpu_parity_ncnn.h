#pragma once
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "gpu/android/gpu_parity_probe.h"
#include <command.h>
#include <pipeline.h>
#include <memory>
#include <string>
#include <vector>

namespace humanvision::gpu {
// Evaluation only. Caller records into its existing worker command and proves
// that submission complete before CollectCompleted. No command/resource creation
// occurs in Record. Readout is 56 logical bytes (64 with ncnn alignment).
class NcnnParityReduction {
public:
    bool Initialize(const ncnn::VulkanDevice*, ncnn::VkAllocator*, ncnn::VkAllocator*, std::string&);
    bool Record(const ncnn::VkMat& actual, const ncnn::VkMat& golden,
                float tolerance, ncnn::VkCompute&, std::string&, bool rgba_golden = false);
    bool CollectCompleted(ParityReduction&) const noexcept;
private:
    std::unique_ptr<ncnn::Pipeline> pipeline_;
    ncnn::VkMat summary_;
    ncnn::Mat cpu_summary_;
    ncnn::Option download_option_;
    std::vector<ncnn::VkMat> bindings_;
    std::vector<ncnn::vk_constant_type> constants_;
};
}
#endif
