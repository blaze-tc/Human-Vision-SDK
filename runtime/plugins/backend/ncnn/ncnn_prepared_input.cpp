#include "plugins/backend/ncnn/ncnn_prepared_input.h"

namespace humanvision::runtime::ncnn_backend {
namespace {
bool SameRef(const HV_GpuPreparedRefV1& a, const HV_GpuPreparedRefV1& b) noexcept {
    return a.struct_size >= sizeof(a) && a.api_version == HV_GPU_PREPARED_API_V1 &&
        a.token == b.token && a.generation == b.generation &&
        a.frame_id == b.frame_id && a.timestamp_us == b.timestamp_us &&
        a.flags == 0 && a.reserved == 0;
}
}
bool PreparedInputState::Initialize(uint64_t generation) noexcept {
    if (stage_ != Stage::Idle || !generation || generation <= generation_) return false;
    generation_ = generation;
    return true;
}
bool PreparedInputState::Begin(const HV_GpuFrameRefV1& frame, HV_GpuPreparedRefV1& ref) noexcept {
    ref = {};
    if (stage_ != Stage::Idle || !generation_ || frame.generation != generation_ ||
        frame.struct_size < sizeof(frame) || frame.api_version != HV_GPU_FRAME_API_V1 ||
        frame.frame_id < 0 || frame.flags || next_token_ == 0) return false;
    active_ = {sizeof(active_), HV_GPU_PREPARED_API_V1, next_token_++, frame.generation,
        frame.frame_id, frame.timestamp_us, 0, 0};
    stage_ = Stage::Copying;
    ref = active_;
    return true;
}
bool PreparedInputState::ProveGpuCopy() noexcept {
    if (stage_ != Stage::Copying) return false;
    stage_ = Stage::Proved;
    return true;
}
bool PreparedInputState::ReleaseSourceRole() noexcept {
    if (stage_ != Stage::Proved) return false;
    stage_ = Stage::Detached;
    return true;
}
bool PreparedInputState::Ready(const HV_GpuPreparedRefV1& ref) const noexcept {
    return stage_ == Stage::Detached && SameRef(ref, active_);
}
bool PreparedInputState::Consume(const HV_GpuPreparedRefV1& ref) noexcept {
    if (!Ready(ref)) return false;
    active_ = {};
    stage_ = Stage::Idle;
    return true;
}
bool PreparedInputState::Discard(const HV_GpuPreparedRefV1& ref) noexcept {
    return Consume(ref);
}
void PreparedInputState::Quarantine() noexcept { stage_ = Stage::Quarantined; }
bool PreparedInputState::Quarantined() const noexcept { return stage_ == Stage::Quarantined; }
bool PreparedInputState::Idle() const noexcept { return stage_ == Stage::Idle; }
bool PreparedInputState::RequiresProcessLifetimeRetention(bool terminal_gpu_fault) const noexcept {
    return terminal_gpu_fault || stage_ == Stage::Copying || stage_ == Stage::Proved ||
        stage_ == Stage::Quarantined;
}
}

#if defined(__ANDROID__)
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/backend/ncnn/ncnn_preprocess.h"

namespace humanvision::runtime::ncnn_backend {
bool PreparedGpuResources::Initialize(const ncnn::VulkanDevice* device,
                                      const ncnn::Net& net, ncnn::VkAllocator* blob,
                                      ncnn::VkAllocator* staging,
                                      const InputContract& contract) {
    if (!device || !blob || !staging || contract.width != 320 ||
        contract.height != 320 || contract.output_type != HV_GPU_TENSOR_FP16 ||
        contract.output_elempack != 1 || contract.output_blobs !=
            std::vector<std::string>{"cls", "bbox"}) return false;
    compute = std::make_unique<ncnn::VkCompute>(device);
    normalized.create(320, 320, 3, sizeof(float), 1, blob);
    tensor.create(320, 320, 3, sizeof(uint16_t), 1, blob);
    if (normalized.empty() || tensor.empty()) return false;
    extractor = std::make_unique<ncnn::Extractor>(net.create_extractor());
    extractor->set_blob_vkallocator(blob);
    extractor->set_workspace_vkallocator(blob);
    extractor->set_staging_vkallocator(staging);
    pristine_extractor = std::make_unique<ncnn::Extractor>(*extractor);
    gpu_outputs.resize(contract.output_blobs.size());
    fp32_outputs.resize(contract.output_blobs.size());
    cpu_outputs.resize(contract.output_blobs.size());
    dense_outputs.resize(contract.output_blobs.size());
    return true;
}
PreparedGpuResources::~PreparedGpuResources() {
    pristine_extractor.reset();
    extractor.reset();
    gpu_outputs.clear(); fp32_outputs.clear(); cpu_outputs.clear(); dense_outputs.clear();
    tensor.release(); normalized.release(); compute.reset();
}
}
#endif
