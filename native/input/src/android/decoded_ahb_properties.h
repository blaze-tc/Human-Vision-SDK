#pragma once
#include "android_input_gpu.h"
namespace hvinput {
inline bool QueryDecodedAhbProperties(
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb, VkDevice device,
    AHardwareBuffer *buffer, const AHardwareBuffer_Desc &desc,
    InputGpuCapabilities *out) {
  VkAndroidHardwareBufferFormatPropertiesANDROID format{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
  VkAndroidHardwareBufferPropertiesANDROID properties{
      VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &format};
  if (get_ahb(device, buffer, &properties) != VK_SUCCESS)
    return false;
  out->format = format.format;
  out->external_format = format.externalFormat;
  out->format_features = format.formatFeatures;
  out->sampled =
      (format.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
      (desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) != 0;
  out->model = format.suggestedYcbcrModel;
  out->range = format.suggestedYcbcrRange;
  out->x_chroma = format.suggestedXChromaOffset;
  out->y_chroma = format.suggestedYChromaOffset;
  out->components[0] = format.samplerYcbcrConversionComponents.r;
  out->components[1] = format.samplerYcbcrConversionComponents.g;
  out->components[2] = format.samplerYcbcrConversionComponents.b;
  out->components[3] = format.samplerYcbcrConversionComponents.a;
  return true;
}
} // namespace hvinput
