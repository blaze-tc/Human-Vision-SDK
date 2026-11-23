#pragma once
#include "humanvision_image_service_v1.h"
#include "common/frame_envelope.h"
#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace humanvision::runtime {
uint64_t ImageContractFingerprint(std::string_view manifest,std::string_view profile) noexcept;
uint64_t ImagePlanFingerprint(const HV_ImageTransformPlanV1&) noexcept;
bool SameImageIdentity(const HV_ImageIdentityV1&,const HV_ImageIdentityV1&) noexcept;
HV_ImageDescriptorV1 DescribeCopiedImage(const HV_VideoFrame&,const FrameEnvelope&) noexcept;
HV_ImageTransformPlanV1 IdentityImagePlan(const HV_ImageDescriptorV1&,uint64_t contract) noexcept;
int32_t ValidateImageTransform(const HV_ImageDescriptorV1&,const HV_ImageTransformPlanV1&) noexcept;
HV_Result ValidatePreparedImage(const HV_ImageTransformPlanV1&,const HV_PreparedImageLeaseV1&) noexcept;
uint32_t ImagePixelBytes(uint32_t format) noexcept;
void ImageInverseTransform(const HV_ImageTransformPlanV1&,float out[6]) noexcept;

// Generic completion ownership used by synchronous CPU and future async image
// services. Cancellation never returns a Pending hardware slot to the pool.
class PreparedLeasePool {
public:
 int32_t Reserve(uint64_t& token) noexcept;
 bool Complete(uint64_t token) noexcept;
 int32_t Cancel(uint64_t token) noexcept;
 int32_t Status(uint64_t token) const noexcept;
 int32_t Release(uint64_t token) noexcept;
 static uint32_t Index(uint64_t token) noexcept {return static_cast<uint32_t>(token&3u);}
private:
 enum class State {Free,Pending,Ready,CancelPending,CancelReady};
 struct Slot {uint64_t token=0;State state=State::Free;};
 mutable std::mutex mutex_;
 std::array<Slot,3> slots_{};
 uint64_t next_=0;
};
}
