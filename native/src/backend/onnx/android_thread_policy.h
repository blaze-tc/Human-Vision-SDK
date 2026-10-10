#pragma once
#include "backend/onnx/onnx_runtime_backend.h"
namespace humanvision {
// Private backend policy; no changes to Unity or C ABI. Accelerator providers
// retain their dedicated pools. CPU experiments are measured independently.
inline int AndroidOrtIntraOpThreads(OnnxRuntimeProvider provider, unsigned processors) {
    if (provider != OnnxRuntimeProvider::Cpu || processors == 0) return 1;
    // OnePlus real FP32 RTMO experiment: 4 threads ~90ms versus 1 ~170-198ms;
    // cap at four rather than starting a pool on every little/big core. No spin.
    return static_cast<int>(processors < 4 ? processors : 4);
}
}
