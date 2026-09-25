#if defined(__ANDROID__) && defined(HV_ANDROID_GPU_GATE)
#include "gpu/android/unity_vulkan_plugin.h"
#include "plugins/backend/ncnn/ncnn_android_session.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {
class Gate {
public:
    static Gate& Get() { static Gate gate; return gate; }
    bool Begin(void* texture, const char* contract) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable() || !texture || !contract || !*contract ||
            !humanvision::gpu::BeginUnityVulkanSourceLease(texture)) return false;
        contract_ = contract;
        stopped_.store(false);
        converted_.store(0);
        error_.clear();
        worker_ = std::thread([this] { Work(); });
        return true;
    }
    void End() {
        stopped_.store(true);
        if (worker_.joinable()) worker_.join();
        humanvision::gpu::EndUnityVulkanSourceLease();
    }
    HV_Result Submit(const HV_AndroidGpuSubmissionV1& frame, void** data, int* event_id) {
        if (!data || !event_id) return HV_ERR_INVALID_ARGUMENT;
        *event_id = 0;
        const auto result = humanvision::gpu::PrepareUnityVulkanFrame(frame, data);
        if (result == humanvision::gpu::BridgeResult::Busy && *data) *event_id = 1;
        return humanvision::gpu::AndroidBridgeResultCode(result);
    }
    void Status(HV_AndroidGpuBridgeStatusV1& status, uint64_t& converted,
                char* error, uint32_t capacity) {
        humanvision::gpu::GetUnityVulkanProducerStatus(status);
        converted = converted_.load();
        std::string message;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            message = error_;
        }
        if (message.empty()) message = humanvision::gpu::UnityVulkanProducerGateError();
        if (error && capacity) {
            const auto length = std::min<size_t>(message.size(), capacity - 1);
            std::memcpy(error, message.data(), length);
            error[length] = 0;
        }
    }
    uint32_t Probe(char* probe, uint32_t capacity) {
        const char* diagnostic = humanvision::gpu::UnityVulkanProducerGateProbe();
        const size_t length = std::strlen(diagnostic);
        if (probe && capacity) {
            const auto copied = std::min<size_t>(length, capacity - 1);
            std::memcpy(probe, diagnostic, copied);
            probe[copied] = 0;
        }
        return static_cast<uint32_t>(length + 1);
    }
private:
    void Fail(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = error;
        stopped_.store(true);
    }
    void Work() {
        using namespace humanvision;
        auto* bridge = gpu::UnityVulkanProducerBridge();
        while (!stopped_.load() && bridge->IsClosed())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (stopped_.load()) return;
        gpu::VulkanDeviceContext context{};
        if (!gpu::UnityVulkanProducerContext(context)) { Fail("Gate Unity Vulkan context unavailable"); return; }
        runtime::ncnn_backend::HostContext host{};
        host.unity_device = context;
        host.bridge = bridge;
        runtime::ncnn_backend::AndroidSession session;
        std::string error;
        if (!session.InitializeGate(contract_, host, error)) { Fail(error); return; }
        const auto& contract = session.GateContract();
        while (!stopped_.load()) {
            gpu::ConsumerFrame lease;
            const auto claimed = bridge->ClaimConsumer(lease);
            if (claimed == gpu::SlotResult::NoReady || claimed == gpu::SlotResult::Busy) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (claimed != gpu::SlotResult::Ok) { Fail("Gate AHB consumer claim failed"); break; }
            HV_GpuFrameRefV1 frame{};
            frame.struct_size = sizeof(frame); frame.api_version = HV_GPU_FRAME_API_V1;
            frame.opaque_slot = &lease;
            frame.width = static_cast<int>(session.GateGenerationContract().width);
            frame.height = static_cast<int>(session.GateGenerationContract().height);
            frame.frame_id = static_cast<int64_t>(lease.metadata.frame_id);
            frame.timestamp_us = lease.metadata.timestamp_us;
            frame.generation = lease.metadata.generation;
            frame.image_format = HV_GPU_IMAGE_RGBA8_UNORM;
            HV_GpuImageTransformV1 transform{};
            transform.struct_size = sizeof(transform); transform.api_version = HV_GPU_FRAME_API_V1;
            transform.source_rect_px = {0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)};
            transform.output_width = contract.width; transform.output_height = contract.height;
            transform.output_type = contract.output_type;
            transform.output_elempack = static_cast<uint32_t>(contract.output_elempack);
            transform.channel_order = contract.channel_order;
            for (int i = 0; i < 3; ++i) { transform.mean[i] = contract.mean[i]; transform.norm[i] = contract.norm[i]; }
            uint32_t count = 0;
            const auto result = session.Run(frame, transform, nullptr, 0, count, error);
            if (result != HV_OK) { Fail(error); break; }
            converted_.fetch_add(1);
        }
    }
    std::mutex mutex_;
    std::thread worker_;
    std::atomic<bool> stopped_{true};
    std::atomic<uint64_t> converted_{0};
    std::string contract_, error_;
};
}

extern "C" {
__attribute__((visibility("default"))) int HV_CALL HV_AndroidGpuGateBegin(void* texture, const char* contract) {
    return Gate::Get().Begin(texture, contract) ? 0 : -1;
}
__attribute__((visibility("default"))) void HV_CALL HV_AndroidGpuGateEnd() { Gate::Get().End(); }
__attribute__((visibility("default"))) int HV_CALL HV_AndroidGpuGateSubmit(
    const HV_AndroidGpuSubmissionV1* frame, void** data, int* event_id) {
    if (!frame || frame->struct_size != sizeof(*frame) || frame->api_version != HV_ANDROID_GPU_API_V1)
        return HV_ERR_INVALID_ARGUMENT;
    return Gate::Get().Submit(*frame, data, event_id);
}
__attribute__((visibility("default"))) void HV_CALL HV_AndroidGpuGateStatus(
    HV_AndroidGpuBridgeStatusV1* status, uint64_t* converted, char* error, uint32_t capacity) {
    if (!status || !converted || status->struct_size != sizeof(*status) ||
        status->api_version != HV_ANDROID_GPU_API_V1) return;
    Gate::Get().Status(*status, *converted, error, capacity);
}
__attribute__((visibility("default"))) uint32_t HV_CALL HV_AndroidGpuGateProbe(char* probe, uint32_t capacity) {
    return Gate::Get().Probe(probe, capacity);
}
}
#endif
