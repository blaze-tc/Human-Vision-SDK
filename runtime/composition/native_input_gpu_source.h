#pragma once
#include "host/gpu_runtime_host.h"
#include "gpu/android/consumer_image_source.h"
#include "humanvision_input_native.h"
#include <atomic>

namespace humanvision::runtime {
struct NativeInputGpuApi {
 int (HV_INPUT_CALL *device)(HV_InputHandle,HV_InputNativeDeviceV1*)=nullptr;
 int (HV_INPUT_CALL *pool)(HV_InputHandle,HV_InputNativePoolV1*)=nullptr;
 int (HV_INPUT_CALL *acquire)(HV_InputHandle,uint64_t,HV_InputNativeFrameV1*)=nullptr;
 int (HV_INPUT_CALL *release)(HV_InputHandle,uint32_t,uint64_t,uint64_t)=nullptr;
};
class NativeInputGpuSource final : public GpuConsumerSource, public gpu::ConsumerImageSource {
public:
 using Arrival=void (*)(void*,const HV_InputNativeFrameV1&);
 NativeInputGpuSource(HV_InputHandle input,NativeInputGpuApi api,int64_t offset,
                      uint64_t source_id,uint64_t epoch,void* context=nullptr,Arrival arrival=nullptr,uint64_t clock_id=0);
 bool Initialize(std::string& error);
 gpu::SlotResult Claim(gpu::ConsumerFrame&) noexcept override;
 gpu::SlotResult ClaimDropped(gpu::ConsumerFrame&) noexcept override {return gpu::SlotResult::NoReady;}
 gpu::SlotResult Retire(gpu::ConsumerFrame& frame) noexcept override {return RetireConsumer(frame,gpu::CompletionProof::GpuQuiescent);}
 void Quarantine(gpu::ConsumerFrame& frame) noexcept override {(void)QuarantineConsumer(frame);}
 bool DrainUnsubmitted(gpu::ConsumerFrame&) noexcept override;
 uint64_t Generation() const noexcept override {return generation_.load();}
 uint32_t Width() const noexcept override {return pool_.width;}
 uint32_t Height() const noexcept override {return pool_.height;}
 gpu::DeviceIdentity Identity() const override {return identity_;}
 gpu::SlotResult RetainGeneration(gpu::ConsumerGeneration&,void (*)(uintptr_t) noexcept) noexcept override;
 gpu::SlotResult RetireConsumer(gpu::ConsumerFrame&,gpu::CompletionProof) noexcept override;
 gpu::SlotResult QuarantineConsumer(gpu::ConsumerFrame&) noexcept override;
 bool IsQuarantined() const noexcept {return quarantined_.load();}
 uint64_t ErrorCount() const noexcept {return errors_.load();}
private:
 HV_InputHandle input_;NativeInputGpuApi api_;int64_t offset_;
 uint64_t source_id_,epoch_,sequence_=0,clock_id_=0;
 std::atomic<uint64_t> generation_{0};std::atomic<bool> quarantined_{false};
 std::atomic<uint64_t> errors_{0};std::atomic<bool> failed_{false};
 HV_InputNativePoolV1 pool_{};gpu::DeviceIdentity identity_{};
 void* context_;Arrival arrival_;
};
}
