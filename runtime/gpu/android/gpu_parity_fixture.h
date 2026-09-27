#pragma once
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include "gpu/android/gpu_parity_ncnn.h"
#include "gpu/android/gpu_parity_fixture_data.h"
#include <array>
#include <memory>

namespace humanvision::gpu {
class NcnnParityStages {
public:
    bool Initialize(const ncnn::VulkanDevice*,ncnn::VkAllocator*,ncnn::VkAllocator*,
                    const std::array<uint8_t,16>& device_uuid,std::string&);
    bool Record(const ncnn::VkMat&,const ncnn::VkMat&,const ncnn::VkMat&,
                ncnn::VkCompute&,uint64_t generation,uint64_t source_id,std::string&);
    void ReportCompleted(uint64_t generation,uint64_t source_id,uint32_t slot);
    void ReportDetectorOutput(uint64_t source_id,const char* name,const float* values,size_t count);
private:
    struct DispatchContext {
        NcnnParityStages* owner=nullptr; uint32_t index=0;
        const ncnn::VkMat* actual=nullptr; ncnn::VkCompute* command=nullptr;
        std::string* error=nullptr; bool ready=false;
    };
    static bool DispatchRecord(void*,const ParityGpuView&,const ParityGpuView&,
                               const ParityContract&,uint32_t,ParityReduction&) noexcept;
    static bool DispatchCollect(void*,uint32_t,ParityReduction&) noexcept;
    std::array<DispatchContext,3> contexts_{};
    std::array<std::unique_ptr<GpuParityProbe>,3> probes_;
    std::array<ParityTicket,3> tickets_{};
    std::array<uint8_t,16> device_uuid_{};
    uintptr_t device_handle_=0;
    std::shared_ptr<const ParityFixture> fixture_;
    std::array<ncnn::VkMat,3> goldens_;
    std::array<NcnnParityReduction,3> reducers_;
    std::unique_ptr<ncnn::Pipeline> import_fault_pipeline_;
    ncnn::VkMat import_fault_scratch_;
    std::array<NcnnParityReduction,8> import_fault_reducers_;
    std::vector<ncnn::VkMat> import_fault_bindings_;
    std::vector<ncnn::vk_constant_type> import_fault_constants_;
    bool import_faults_pending_=false;
    uint64_t import_control_epoch_=0;
    std::array<uint64_t,2> detector_sources_{};
    uint32_t completed_=0;
    bool pending_=false;
};
}
#endif
