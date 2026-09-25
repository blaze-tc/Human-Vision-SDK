#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/backend/ncnn/ncnn_preprocess.h"
#include "plugins/backend/ncnn/ncnn_input_delivery.h"
#include "plugins/backend/ncnn/ncnn_model_options.h"
#include <gtest/gtest.h>

using humanvision::runtime::ncnn_backend::InputContract;
using humanvision::runtime::ncnn_backend::ParseInputContract;

namespace {
constexpr const char* good = R"({"image_format":"rgba8-unorm","color_order":"rgb","normalization":{"mean":[0,0,0],"norm":[0.0039215686,0.0039215686,0.0039215686]},"tensor_dtype":"fp32","elempack":1,"width":320,"height":320,"input_blob":"in0","output_blobs":["cls","bbox"]})";
}

TEST(NcnnInputContract, AcceptsExactRgbaToRgbTensorCases) {
  for (const auto& [dtype, pack, cast] : {std::tuple{"fp32",1,1}, {"fp16",1,2}, {"fp16",4,2}}) {
    auto json = nlohmann::json::parse(good);
    json["tensor_dtype"] = dtype;
    json["elempack"] = pack;
    InputContract contract;
    std::string error;
    ASSERT_TRUE(ParseInputContract(json, contract, error)) << error;
    EXPECT_EQ(contract.output_type, cast == 1 ? HV_GPU_TENSOR_FP32 : HV_GPU_TENSOR_FP16);
    EXPECT_EQ(contract.output_elempack, pack);
    EXPECT_EQ(contract.cast_type_to, cast);
    EXPECT_EQ(contract.channel_order, 1u);
    EXPECT_EQ(contract.input_blob, "in0");
    EXPECT_EQ(contract.output_blobs, (std::vector<std::string>{"cls", "bbox"}));
  }
}

TEST(NcnnInputContract, RejectsEveryUnspecifiedField) {
  for (const auto& path : {"color_order", "normalization", "tensor_dtype", "elempack", "width", "height", "input_blob", "output_blobs"}) {
    auto json = nlohmann::json::parse(good);
    json.erase(path);
    InputContract contract;
    std::string error;
    EXPECT_FALSE(ParseInputContract(json, contract, error)) << path;
    EXPECT_FALSE(error.empty()) << path;
  }
  for (const auto& key : {"mean", "norm"}) {
    auto json = nlohmann::json::parse(good);
    json["normalization"].erase(key);
    InputContract contract;
    std::string error;
    EXPECT_FALSE(ParseInputContract(json, contract, error)) << key;
  }
}

TEST(NcnnDetectorPack1Input, ThreeChannelFp16Contract) {
  auto json = nlohmann::json::parse(good);
  json["tensor_dtype"] = "fp16";
  json["elempack"] = 1;
  InputContract contract;
  std::string error;
  ASSERT_TRUE(ParseInputContract(json, contract, error)) << error;
  EXPECT_EQ(humanvision::runtime::ncnn_backend::NormalizedChannelCount(contract.output_elempack), 3);
  EXPECT_EQ(contract.output_elempack, 1);
  EXPECT_EQ(contract.output_type, HV_GPU_TENSOR_FP16);
  EXPECT_EQ(contract.cast_type_to, 2);
  EXPECT_EQ(contract.input_blob, "in0");
  EXPECT_EQ(contract.output_blobs, (std::vector<std::string>{"cls", "bbox"}));
}

namespace {
struct FakeTensor {
  int c;
  int elempack;
  int bits;
  int elembits() const { return bits; }
};
struct RecordingExtractor {
  int clear_calls = 0;
  int input_calls = 0;
  FakeTensor received{};
  void clear() { ++clear_calls; }
  int input(const char* blob, const FakeTensor& tensor) {
    ++input_calls;
    received = tensor;
    return std::string(blob) == "in0" ? 0 : -1;
  }
};
}

TEST(NcnnDetectorPack1Input, ExtractorReceivesOnlyThreeChannelFp16Pack1) {
  using humanvision::runtime::ncnn_backend::DeliverInput;
  using humanvision::runtime::ncnn_backend::InputDeliveryResult;
  RecordingExtractor extractor;
  EXPECT_EQ(DeliverInput(extractor, "in0", FakeTensor{1, 4, 16}, true),
            InputDeliveryResult::InvalidTensor);
  EXPECT_EQ(extractor.input_calls, 0);
  EXPECT_EQ(DeliverInput(extractor, "in0", FakeTensor{3, 1, 32}, true),
            InputDeliveryResult::InvalidTensor);
  EXPECT_EQ(extractor.input_calls, 0);
  EXPECT_EQ(DeliverInput(extractor, "in0", FakeTensor{4, 1, 16}, true),
            InputDeliveryResult::InvalidTensor);
  EXPECT_EQ(extractor.input_calls, 0);
  EXPECT_EQ(DeliverInput(extractor, "in0", FakeTensor{3, 1, 16}, true),
            InputDeliveryResult::Ok);
  ASSERT_EQ(extractor.input_calls, 1);
  EXPECT_EQ(extractor.received.c, 3);
  EXPECT_EQ(extractor.received.elempack, 1);
  EXPECT_EQ(extractor.received.elembits(), 16);
  EXPECT_EQ(extractor.clear_calls, 1);
  EXPECT_EQ(DeliverInput(extractor, "wrong", FakeTensor{3, 1, 16}, true),
            InputDeliveryResult::Rejected);
  EXPECT_EQ(extractor.input_calls, 2);
  EXPECT_EQ(DeliverInput(extractor, "in0", FakeTensor{1, 4, 16}, false),
            InputDeliveryResult::Ok);
  EXPECT_EQ(extractor.received.elempack, 4);
}

TEST(NcnnBackendOptions, BodyRequiresExplicitNonSubgroupFp32Arithmetic) {
  using humanvision::runtime::ncnn_backend::BackendOptions;
  using humanvision::runtime::ncnn_backend::ParseBackendOptions;
  nlohmann::json model = {
    {"role", "body"},
    {"backend_options", {{"use_subgroup_ops", false}, {"use_fp16_arithmetic", false}}}
  };
  BackendOptions options{};
  std::string error;
  ASSERT_TRUE(ParseBackendOptions(model, options, error)) << error;
  EXPECT_FALSE(options.use_subgroup_ops);
  EXPECT_FALSE(options.use_fp16_arithmetic);

  for (const auto* missing : {"use_subgroup_ops", "use_fp16_arithmetic"}) {
    auto changed = model;
    changed["backend_options"].erase(missing);
    EXPECT_FALSE(ParseBackendOptions(changed, options, error)) << missing;
  }
  for (const auto* wrong : {"use_subgroup_ops", "use_fp16_arithmetic"}) {
    auto changed = model;
    changed["backend_options"][wrong] = true;
    EXPECT_FALSE(ParseBackendOptions(changed, options, error)) << wrong;
    changed["backend_options"][wrong] = "false";
    EXPECT_FALSE(ParseBackendOptions(changed, options, error)) << wrong;
  }
}

TEST(NcnnBackendOptions, DetectorRequiresExplicitSubgroupFp16Arithmetic) {
  using humanvision::runtime::ncnn_backend::BackendOptions;
  using humanvision::runtime::ncnn_backend::ParseBackendOptions;
  nlohmann::json model = {
    {"role", "detector"},
    {"backend_options", {{"use_subgroup_ops", true}, {"use_fp16_arithmetic", true}}}
  };
  BackendOptions options{};
  std::string error;
  ASSERT_TRUE(ParseBackendOptions(model, options, error)) << error;
  EXPECT_TRUE(options.use_subgroup_ops);
  EXPECT_TRUE(options.use_fp16_arithmetic);

  auto missing = model;
  missing.erase("backend_options");
  EXPECT_FALSE(ParseBackendOptions(missing, options, error));
  auto extra = model;
  extra["backend_options"]["surprise"] = false;
  EXPECT_FALSE(ParseBackendOptions(extra, options, error));
  auto unknown = model;
  unknown["role"] = "other";
  EXPECT_FALSE(ParseBackendOptions(unknown, options, error));
}

TEST(NcnnBackendOptions, AppliesSelectedRoleFlagsBeforeModelLoad) {
  using humanvision::runtime::ncnn_backend::ApplyBackendOptions;
  using humanvision::runtime::ncnn_backend::BackendOptions;
  struct RecordingOption {
    bool use_subgroup_ops = true;
    bool use_fp16_arithmetic = true;
  } option;
  ApplyBackendOptions(option, BackendOptions{false, false});
  EXPECT_FALSE(option.use_subgroup_ops);
  EXPECT_FALSE(option.use_fp16_arithmetic);
  ApplyBackendOptions(option, BackendOptions{true, true});
  EXPECT_TRUE(option.use_subgroup_ops);
  EXPECT_TRUE(option.use_fp16_arithmetic);
}
