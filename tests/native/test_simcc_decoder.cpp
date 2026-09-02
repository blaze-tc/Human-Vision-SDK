#include "models/rtmpose/simcc_decoder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

namespace {

TEST(SimccDecoder, DecodesKnownPeaksAndRestoresSourceCoordinates) {
    humanvision::Tensor simcc_x;
    simcc_x.name = "simcc_x";
    simcc_x.shape = {1, HV_JOINT_COUNT, 384};
    simcc_x.values.assign(HV_JOINT_COUNT * 384, -1.0F);
    humanvision::Tensor simcc_y;
    simcc_y.name = "simcc_y";
    simcc_y.shape = {1, HV_JOINT_COUNT, 512};
    simcc_y.values.assign(HV_JOINT_COUNT * 512, -1.0F);
    for (int joint = 0; joint < HV_JOINT_COUNT; ++joint) {
        simcc_x.values[static_cast<std::size_t>(joint) * 384U + 20U + joint] =
            0.90F - joint * 0.01F;
        simcc_y.values[static_cast<std::size_t>(joint) * 512U + 40U + joint] =
            0.80F - joint * 0.01F;
    }

    humanvision::PoseAffineTransform transform{};
    transform.center_x = 100.0F;
    transform.center_y = 200.0F;
    transform.scale_width = 192.0F;
    transform.scale_height = 256.0F;
    transform.input_to_source = {1.0F, 0.0F, 4.0F, 0.0F, 1.0F, 8.0F};

    std::array<humanvision::DecodedJoint, HV_JOINT_COUNT> joints{};
    std::string error;
    ASSERT_TRUE(humanvision::DecodeSimcc(
        simcc_x, simcc_y, transform, joints, error)) << error;
    EXPECT_NEAR(joints[0].x_px, 14.0F, 1.0e-6F);
    EXPECT_NEAR(joints[0].y_px, 28.0F, 1.0e-6F);
    EXPECT_NEAR(joints[0].confidence, 0.80F, 1.0e-6F);
    EXPECT_NEAR(joints[16].x_px, 22.0F, 1.0e-6F);
    EXPECT_NEAR(joints[16].y_px, 36.0F, 1.0e-6F);
    EXPECT_NEAR(joints[16].confidence, 0.64F, 1.0e-6F);
}

TEST(SimccDecoder, RejectsMalformedTensorContracts) {
    humanvision::Tensor simcc_x{"simcc_x", {1, 17, 383}, {}};
    simcc_x.values.resize(17U * 383U);
    humanvision::Tensor simcc_y{"simcc_y", {1, 17, 512}, {}};
    simcc_y.values.resize(17U * 512U);
    humanvision::PoseAffineTransform transform{};
    std::array<humanvision::DecodedJoint, HV_JOINT_COUNT> joints{};
    std::string error;
    EXPECT_FALSE(humanvision::DecodeSimcc(
        simcc_x, simcc_y, transform, joints, error));
    EXPECT_NE(error.find("384"), std::string::npos);
}

}  // namespace
