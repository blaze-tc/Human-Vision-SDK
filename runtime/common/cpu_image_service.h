#pragma once
#include "common/image_transform_contract.h"
#include <array>
#include <vector>

namespace humanvision::runtime {
// Synchronous oracle; storage grows only during warm-up/source-size changes.
// One context owns exactly three output slots, never a worker/thread pool.
class CpuImageService {
public:
 explicit CpuImageService(uint64_t maximum_output_bytes=64ull*1024*1024) : maximum_bytes_(maximum_output_bytes) {}
 HV_Result Query(uint32_t version,HV_ImageServiceApiV1*) noexcept;
private:
 struct Slot {std::vector<uint8_t> bytes;HV_PreparedImageLeaseV1 lease{};};
 static int32_t HV_CALL Support(void*,const HV_ImageDescriptorV1*,const HV_ImageTransformPlanV1*,HV_ErrorBufferV1*);
 static int32_t HV_CALL Submit(void*,const HV_ImageDescriptorV1*,const HV_ImageTransformPlanV1*,HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
 static int32_t HV_CALL Wait(void*,HV_PreparedImageLeaseV1*,uint32_t,HV_ErrorBufferV1*);
 static int32_t HV_CALL Cancel(void*,HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
 static int32_t HV_CALL Release(void*,const HV_PreparedImageLeaseV1*,HV_ErrorBufferV1*);
 PreparedLeasePool pool_;
 std::mutex mutex_;
 std::array<Slot,3> slots_;
 uint64_t maximum_bytes_;
};
}
