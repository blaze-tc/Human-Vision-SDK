#pragma once
#include <vulkan/vulkan_core.h>
namespace hvinput {
enum class YcbcrDeclarationFailure { None, Disabled, DuplicateSampler, DuplicateVulkan11, ConflictingAliases };
struct YcbcrDeclarations {
  bool declared = false, enabled = false;
  YcbcrDeclarationFailure failure = YcbcrDeclarationFailure::None;
  bool Valid() const { return failure == YcbcrDeclarationFailure::None; }
  bool ShouldPrepend() const { return Valid() && !declared; }
};
// Reads the original Vulkan chain without changing any feature or pNext link.
inline YcbcrDeclarations ValidateYcbcrDeclarations(const void* chain) {
  YcbcrDeclarations result;
  unsigned sampler_count = 0, vulkan11_count = 0;
  bool disabled = false;
  for (auto* node = reinterpret_cast<const VkBaseInStructure*>(chain); node; node = node->pNext) {
    if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES) {
      ++sampler_count;
      disabled = disabled || !reinterpret_cast<const VkPhysicalDeviceSamplerYcbcrConversionFeatures*>(node)->samplerYcbcrConversion;
    }
#ifdef VK_VERSION_1_2
    if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES) {
      ++vulkan11_count;
      disabled = disabled || !reinterpret_cast<const VkPhysicalDeviceVulkan11Features*>(node)->samplerYcbcrConversion;
    }
#endif
  }
  result.declared = sampler_count || vulkan11_count;
  result.enabled = result.declared && !disabled;
  // Classification and admission are independent of chain order. Vulkan11
  // aggregate and sampler-specific declarations may not coexist, even true/true.
  if (sampler_count > 1) result.failure = YcbcrDeclarationFailure::DuplicateSampler;
  else if (vulkan11_count > 1) result.failure = YcbcrDeclarationFailure::DuplicateVulkan11;
  else if (sampler_count && vulkan11_count) result.failure = YcbcrDeclarationFailure::ConflictingAliases;
  else if (disabled) result.failure = YcbcrDeclarationFailure::Disabled;
  return result;
}
// The hook places physical prerequisite queries and the actual creator inside
// this gate. Invalid declarations cannot enter that callback.
template<class Create> VkResult CreateIfYcbcrValid(const YcbcrDeclarations& declarations, Create&& create) {
  if (!declarations.Valid()) return VK_ERROR_FEATURE_NOT_PRESENT;
  return create();
}
}
