#pragma once
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "gpu/android/gpu_parity_probe.h"
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
namespace humanvision::gpu {
struct ParityFixture;
class ImageParityReduction {
public:
    ~ImageParityReduction();
    bool Initialize(VkPhysicalDevice,VkDevice,const std::array<uint8_t,16>& device_uuid);
    bool Record(VkCommandBuffer,VkImage,VkImageView,VkImageLayout,
                VkImage,VkImageView,VkImageLayout,uint64_t,uint64_t,uint32_t);
    void ReportCompleted();
private:
    struct DispatchContext { ImageParityReduction* owner=nullptr;uint32_t index=0; };
    static bool DispatchRecord(void*,const ParityGpuView&,const ParityGpuView&,
                               const ParityContract&,uint32_t,ParityReduction&) noexcept;
    static bool DispatchCollect(void*,uint32_t,ParityReduction&) noexcept;
    bool InitializeFaults(VkPhysicalDevice);
    void RecordFaults(VkCommandBuffer);
    void RecordReduction(VkCommandBuffer,VkImageView,VkImageLayout,uint32_t);
    std::array<DispatchContext,2> contexts_{};
    std::array<std::unique_ptr<GpuParityProbe>,2> probes_;
    std::array<ParityTicket,2> tickets_{};
    std::array<uint8_t,16> device_uuid_{};
    VkCommandBuffer command_=VK_NULL_HANDLE;
    std::array<VkImageView,2> views_{};
    bool completed_proof_=false;
    VkDevice device_=VK_NULL_HANDLE;
    VkPipeline pipeline_=VK_NULL_HANDLE;
    VkPipelineLayout layout_=VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_=VK_NULL_HANDLE;
    VkDescriptorPool pool_=VK_NULL_HANDLE;
    VkSampler sampler_=VK_NULL_HANDLE;
    std::array<VkDescriptorSet,18> sets_{};
    std::array<VkBuffer,19> buffers_{};
    std::array<VkDeviceMemory,19> memories_{};
    std::array<void*,19> mapped_{};
    VkImage fault_image_=VK_NULL_HANDLE;
    VkImageView fault_view_=VK_NULL_HANDLE;
    VkDeviceMemory fault_memory_=VK_NULL_HANDLE;
    VkPipeline fault_pipeline_=VK_NULL_HANDLE;
    VkPipelineLayout fault_layout_=VK_NULL_HANDLE;
    VkDescriptorSetLayout fault_set_layout_=VK_NULL_HANDLE;
    std::array<VkDescriptorSet,2> fault_sets_{};
    bool faults_pending_=false;
    uint32_t next_fault_=0;
    std::shared_ptr<const ParityFixture> fixture_;
    uint64_t generation_=0,source_=0;
    uint32_t slot_=0;
    uint32_t completed_=0;
    bool pending_=false;
};
}
#endif
