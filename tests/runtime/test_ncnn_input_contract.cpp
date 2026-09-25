#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/backend/ncnn/ncnn_preprocess.h"
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
