#pragma once
#if defined(__ANDROID__) && defined(HV_ANDROID_R4_PARITY)
#include <array>
#include "gpu/android/gpu_parity_probe.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace humanvision::gpu {
struct ParitySummary;
struct ParityFixture {
    uint32_t width=0,height=0;
    int case_index=-1;
    uint32_t expected_people=0;
    std::string manifest_hash;
    std::string root;
    std::array<std::string,3> hashes;
    std::array<std::vector<unsigned char>,3> bytes;
    // Keep preceding image summaries active until every imported-buffer control
    // has completed on its own immutable-source submission.
    mutable ParityControlEpoch import_controls;
};
std::shared_ptr<const ParityFixture> CurrentParityFixture();
int RequestedParityCopyPath();
std::array<uint8_t,32> ParityHashBytes(const std::string&);
void ReportParitySummary(const char* stage,const ParitySummary&,const ParityFixture&,
                         uint32_t slot,int golden_index,uintptr_t device);
bool CompileParityImageShader(std::vector<uint32_t>& spirv);
bool CompileParityShaderSource(const char*,std::vector<uint32_t>& spirv);
}
#endif
