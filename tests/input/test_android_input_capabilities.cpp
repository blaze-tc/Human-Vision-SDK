#include "android_input_gpu.h"
#include "input_ycbcr_declarations.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
using namespace hvinput;
static InputGpuCapabilities Supported() {
  InputGpuCapabilities c{};
  c.external_format = 123; c.sampled = c.physical_ycbcr = true;
  c.physical_sync_fd = c.logical_ycbcr = c.logical_sync_fd = true;
  c.logical_ahb = c.logical_proven = true;
  return c;
}
static void CheckDeclarations(const char* name) {
  VkPhysicalDeviceSamplerYcbcrConversionFeatures sampler{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES, nullptr, VK_TRUE};
  auto second_sampler=sampler;
  VkPhysicalDeviceVulkan11Features aggregate{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  aggregate.samplerYcbcrConversion=VK_TRUE;
  auto second_aggregate=aggregate;
  VkPhysicalDevice16BitStorageFeatures unrelated{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
  unrelated.storageBuffer16BitAccess=VK_TRUE;
  const void* chain=&sampler;
  auto expected=YcbcrDeclarationFailure::None;
  bool prepend=false;
  if(!std::strcmp(name,"DisabledSamplerBeforeEnabledVulkan11")) {
    sampler.samplerYcbcrConversion=VK_FALSE;sampler.pNext=&aggregate;
    expected=YcbcrDeclarationFailure::ConflictingAliases;
  } else if(!std::strcmp(name,"EnabledVulkan11BeforeDisabledSampler")) {
    sampler.samplerYcbcrConversion=VK_FALSE;aggregate.pNext=&sampler;chain=&aggregate;
    expected=YcbcrDeclarationFailure::ConflictingAliases;
  } else if(!std::strcmp(name,"DuplicateSamplerDeclarations")) {
    sampler.pNext=&second_sampler;expected=YcbcrDeclarationFailure::DuplicateSampler;
  } else if(!std::strcmp(name,"DuplicateVulkan11Declarations")) {
    aggregate.pNext=&second_aggregate;chain=&aggregate;expected=YcbcrDeclarationFailure::DuplicateVulkan11;
  } else if(!std::strcmp(name,"EnabledAliasesCoexist")) {
    sampler.pNext=&aggregate;expected=YcbcrDeclarationFailure::ConflictingAliases;
  } else if(!std::strcmp(name,"EnabledAliasesCoexistReverse")) {
    aggregate.pNext=&sampler;chain=&aggregate;expected=YcbcrDeclarationFailure::ConflictingAliases;
  } else if(!std::strcmp(name,"DisabledSamplerDeclaration")) {
    sampler.samplerYcbcrConversion=VK_FALSE;expected=YcbcrDeclarationFailure::Disabled;
  } else if(!std::strcmp(name,"DisabledVulkan11Declaration")) {
    aggregate.samplerYcbcrConversion=VK_FALSE;chain=&aggregate;expected=YcbcrDeclarationFailure::Disabled;
  } else if(!std::strcmp(name,"EnabledSamplerPreservesChain")) {
    unrelated.pNext=&sampler;chain=&unrelated;
  } else if(!std::strcmp(name,"EnabledVulkan11PreservesChain")) {
    unrelated.pNext=&aggregate;chain=&unrelated;aggregate.multiview=VK_TRUE;
  } else if(!std::strcmp(name,"AbsentDeclarationAllowsPrepend")) {
    chain=&unrelated;prepend=true;
  } else throw std::runtime_error("unknown declaration case");
  const auto sampler_before=sampler, second_sampler_before=second_sampler;
  const auto aggregate_before=aggregate, second_aggregate_before=second_aggregate;
  const auto unrelated_before=unrelated;
  const auto result=ValidateYcbcrDeclarations(chain);
  int create_calls=0;
  const auto create_result=CreateIfYcbcrValid(result,[&] {++create_calls;return VK_SUCCESS;});
  if(expected==YcbcrDeclarationFailure::None) {
    if(create_calls!=1 || create_result!=VK_SUCCESS) throw std::runtime_error("valid declaration did not reach creator");
  } else if(create_calls || create_result!=VK_ERROR_FEATURE_NOT_PRESENT) throw std::runtime_error("invalid declaration reached underlying create callback");
  if(result.failure!=expected) throw std::runtime_error("actual production declaration validator accepted/misclassified invalid chain");
  if(result.ShouldPrepend()!=prepend) throw std::runtime_error("duplicate prepend or absent augmentation rejected");
  if(expected==YcbcrDeclarationFailure::None && !prepend && (!result.declared || !result.enabled)) throw std::runtime_error("single enabled declaration rejected");
  if(std::memcmp(&sampler,&sampler_before,sizeof(sampler)) || std::memcmp(&second_sampler,&second_sampler_before,sizeof(second_sampler)) || std::memcmp(&aggregate,&aggregate_before,sizeof(aggregate)) || std::memcmp(&second_aggregate,&second_aggregate_before,sizeof(second_aggregate)) || std::memcmp(&unrelated,&unrelated_before,sizeof(unrelated))) throw std::runtime_error("original chain/feature fields changed");
}
int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("one named case required");
    auto c = Supported();
    if (std::strcmp(argv[1], "MissingExternalFormatFailsExplicitly") == 0) {
      c.external_format = 0;
      if (AdmitDecodedBuffer(c) != CapabilityFailure::MissingExternalFormat) throw std::runtime_error("missing external format admitted");
    } else if (std::strcmp(argv[1], "MissingSyncFdCannotAdmit") == 0) {
      c.logical_sync_fd = false;
      if (AdmitDecodedBuffer(c) != CapabilityFailure::MissingLogicalSyncFd) throw std::runtime_error("disabled logical sync-fd admitted");
      c = Supported(); c.physical_sync_fd = false;
      if (AdmitDecodedBuffer(c) != CapabilityFailure::MissingPhysicalSyncFd) throw std::runtime_error("unsupported physical sync-fd admitted");
    } else if (std::strcmp(argv[1], "PhysicalSupportIsNotLogicalProof") == 0) {
      c.logical_proven = false;
      if (AdmitDecodedBuffer(c) != CapabilityFailure::LogicalDeviceUnproven) throw std::runtime_error("entrypoints treated as enabled proof");
    } else if (std::strcmp(argv[1], "CompleteCapabilitiesAdmit") == 0) {
      if (AdmitDecodedBuffer(c) != CapabilityFailure::None) throw std::runtime_error("complete capabilities refused");
    } else CheckDeclarations(argv[1]);
    std::cout << "PASS " << argv[1] << '\n'; return 0;
  } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
