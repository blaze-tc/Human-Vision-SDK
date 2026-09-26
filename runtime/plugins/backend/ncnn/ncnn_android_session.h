#pragma once

#if defined(__ANDROID__)
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/backend/ncnn/ncnn_preprocess.h"
#include "plugins/backend/ncnn/ncnn_model_options.h"
#include "plugins/backend/ncnn/ncnn_prepared_input.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include <allocator.h>
#include <android/hardware_buffer.h>
#include <net.h>
#include <array>
#include <memory>
#include <mutex>

namespace humanvision::runtime::ncnn_backend {
class AndroidSession {
public:
    AndroidSession();
    ~AndroidSession();
    AndroidSession(const AndroidSession&) = delete;
    AndroidSession& operator=(const AndroidSession&) = delete;
    bool Initialize(const HV_GpuBackendConfigV1&, const HV_GpuDeviceContextV1&, std::string& error);
#if defined(HV_ANDROID_GPU_GATE)
    bool InitializeGate(const std::string& input_contract_json,
                        const HostContext& host, std::string& error);
    const InputContract& GateContract() const noexcept { return contract_; }
    const gpu::SlotContract& GateGenerationContract() const noexcept { return generation_.contract; }
    bool VerifyPreparedGoldenForGate(const HV_GpuPreparedRefV1&, std::string& diagnostic);
    uint64_t GatePreparedOutputDownloadCalls() const noexcept {
        return prepared_ ? prepared_->output_download_calls : 0;
    }
#endif
    HV_Result Run(const HV_GpuFrameRefV1&, const HV_GpuImageTransformV1&,
                  HV_TensorViewV1*, uint32_t capacity, uint32_t& count, std::string& error);
    HV_Result Prepare(const HV_GpuFrameRefV1&, const HV_GpuImageTransformV1&,
                      HV_GpuPreparedRefV1&, std::string& error);
    HV_Result RunPrepared(const HV_GpuPreparedRefV1&, HV_TensorViewV1*,
                          uint32_t capacity, uint32_t& count, std::string& error);
    HV_Result DiscardPrepared(const HV_GpuPreparedRefV1&, std::string& error);
    void QuarantinePrepared(void* opaque_slot) noexcept;
    HV_Result Info(HV_BackendSessionInfoV1& info) const;
    void RetireUnsubmitted(void* opaque_slot) noexcept;
    // Internal worker boundary: one borrowed slot spans detector and every pose ROI.
    bool FinishObservation(gpu::ConsumerFrame& frame, std::string& error) noexcept;
    bool YieldObservation(gpu::ConsumerFrame& frame, std::string& error) noexcept;
    bool IsQuarantined() const noexcept { return terminal_gpu_fault_; }
private:
    struct Slot;
    struct SlotDeleter { void operator()(Slot*) const noexcept; };
    bool ParseModel(const HV_GpuBackendConfigV1&, std::string& error);
    bool InitializeSlots(gpu::UnityVulkanBridge&, std::string& error);
    bool ValidateTransform(const HV_GpuFrameRefV1&, const HV_GpuImageTransformV1&,
                           const gpu::ConsumerFrame&, std::string& error) const;
    bool DrainDropped(std::string& error);
    bool ReleaseActiveRole(gpu::ConsumerFrame& frame, std::string& error) noexcept;
    static bool CompleteRoleCallback(void* owner, gpu::ConsumerFrame& frame,
                                     bool final_role, std::string& error) noexcept;
    std::mutex run_mutex_;
    std::unique_ptr<ncnn::Net> net_;
    const ncnn::VulkanDevice* device_ = nullptr;
    gpu::UnityVulkanBridge* bridge_ = nullptr;
    bool gpu_instance_lease_ = false;
    bool terminal_gpu_fault_ = false;
    bool gate_mode_ = false;
    gpu::ConsumerFrame* active_consumer_ = nullptr;
    gpu::SlotToken active_token_{};
    ncnn::Option option_;
    BackendOptions backend_options_{};
    bool detector_role_ = false;
    InputContract contract_;
    PreparedInputState prepared_state_;
    std::unique_ptr<PreparedGpuResources> prepared_;
    gpu::ConsumerGeneration generation_{};
    std::array<std::unique_ptr<Slot, SlotDeleter>, gpu::AhbSlotRing::kSlotCount> slots_{};
    std::unique_ptr<GpuPreprocess> preprocess_;
    ncnn::VkAllocator* blob_allocator_ = nullptr;
    ncnn::VkAllocator* staging_allocator_ = nullptr;
    std::string param_path_, bin_path_;
    std::vector<uint64_t> output_byte_limits_;
};
}
#endif
