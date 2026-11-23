#pragma once
#include "gpu/android/unity_vulkan_bridge.h"
#include "gpu/vulkan/vulkan_device_identity.h"
#include <utility>

namespace humanvision::gpu {
// Internal image ownership interface; independent of Unity's graphics context.
// Its lifetime covers all leased frames and all persistent consumer imports.
class ConsumerImageSource {
public:
 virtual ~ConsumerImageSource() = default;
 virtual DeviceIdentity Identity() const = 0;
 virtual SlotResult RetainGeneration(ConsumerGeneration&,void (*)(uintptr_t) noexcept) noexcept = 0;
 virtual SlotResult ClaimDropped(ConsumerFrame&) noexcept = 0;
 virtual SlotResult RetireConsumer(ConsumerFrame&,CompletionProof) noexcept = 0;
 virtual SlotResult QuarantineConsumer(ConsumerFrame&) noexcept = 0;
};
class BridgeConsumerImageSource final : public ConsumerImageSource {
public:
 explicit BridgeConsumerImageSource(UnityVulkanBridge* bridge,DeviceIdentity identity={})
  :bridge_(bridge),identity_(std::move(identity)){}
 DeviceIdentity Identity() const override {return identity_;}
 SlotResult RetainGeneration(ConsumerGeneration& out,void (*retain)(uintptr_t) noexcept) noexcept override {
  return bridge_?bridge_->RetainGeneration(out,retain):SlotResult::Closed;
 }
 SlotResult ClaimDropped(ConsumerFrame& out) noexcept override {return bridge_?bridge_->ClaimDropped(out):SlotResult::Closed;}
 SlotResult RetireConsumer(ConsumerFrame& frame,CompletionProof proof) noexcept override {return bridge_?bridge_->RetireConsumer(frame,proof):SlotResult::Closed;}
 SlotResult QuarantineConsumer(ConsumerFrame& frame) noexcept override {return bridge_?bridge_->QuarantineConsumer(frame):SlotResult::Closed;}
private:
 UnityVulkanBridge* bridge_;
 DeviceIdentity identity_;
};
inline SlotResult RetireUnsubmittedConsumer(ConsumerImageSource& source,ConsumerFrame& frame,
 ProducerProofWait wait,void* context) noexcept {
 if(!frame.claimed||!wait)return SlotResult::Invalid;
 if(frame.role_owner){
  std::string error;
  if(CompleteGpuRole(frame,true,error)&&!frame.claimed)return SlotResult::Ok;
  if(frame.claimed)source.QuarantineConsumer(frame);
  return SlotResult::Closed;
 }
 const bool complete=frame.producer_fd.HasPayload()?wait(context,frame.producer_fd):frame.ncnn_role_complete;
 if(!complete){source.QuarantineConsumer(frame);return SlotResult::Closed;}
 const auto retired=source.RetireConsumer(frame,CompletionProof::GpuQuiescent);
 if(retired!=SlotResult::Ok&&frame.claimed)source.QuarantineConsumer(frame);
 return retired;
}
}
