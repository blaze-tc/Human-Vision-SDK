#pragma once

namespace humanvision::runtime::ncnn_backend {

enum class InputDeliveryResult { Ok, InvalidTensor, Rejected };

// Shared by the Android extractor path and its host-side fake extractor test.
template <typename Extractor, typename Tensor>
InputDeliveryResult DeliverInput(Extractor& extractor, const char* blob,
                                 const Tensor& tensor, bool detector) {
    if (detector &&
        (tensor.c != 3 || tensor.elempack != 1 || tensor.elembits() != 16))
        return InputDeliveryResult::InvalidTensor;
    extractor.clear();
    return extractor.input(blob, tensor) == 0
        ? InputDeliveryResult::Ok : InputDeliveryResult::Rejected;
}

}
