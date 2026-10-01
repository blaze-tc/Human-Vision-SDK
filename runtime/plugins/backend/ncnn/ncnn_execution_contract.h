#pragma once
#include "plugins/backend/ncnn/ncnn_model_options.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"

namespace humanvision::runtime::ncnn_backend {
// This contract only declares execution precision and tensor boundaries. Model
// sizes, decoder names, output shapes and pose semantics belong to pipelines.
inline bool ValidateRawTensorBoundary(const BackendOptions& options,const InputContract& input,
                                      bool local_evaluation_only,std::string& error) {
    if(!options.raw_tensor)return true;
    if(!local_evaluation_only||input.output_type!=HV_GPU_TENSOR_FP32||input.output_elempack!=1||
       input.cast_type_to!=1) {
        error="Raw tensor FP32 execution requires local evaluation FP32 pack1 boundary";return false;
    }
    error.clear();return true;
}
}
