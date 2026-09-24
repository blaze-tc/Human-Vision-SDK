#include "plugins/backend/ncnn/ncnn_preprocess.h"

#include <gtest/gtest.h>

using humanvision::runtime::ncnn_backend::NormalizedChannelCount;
using humanvision::runtime::ncnn_backend::NormalizeRgbPixel;

TEST(NcnnPreprocessContract, PadsPack4InputOnGpu) {
    EXPECT_EQ(NormalizedChannelCount(1), 3);
    EXPECT_EQ(NormalizedChannelCount(4), 4);
    EXPECT_EQ(NormalizedChannelCount(2), 0);
    const float rgb[] = {255.0f, 128.0f, 0.0f};
    const float mean[] = {127.5f, 64.0f, 50.0f};
    const float norm[] = {1.0f / 127.5f, 0.5f, 2.0f};
    float output[] = {-1, -1, -1, -1};
    ASSERT_TRUE(NormalizeRgbPixel(rgb, mean, norm, 4, output));
    EXPECT_FLOAT_EQ(output[0], 1.0f);
    EXPECT_FLOAT_EQ(output[1], 32.0f);
    EXPECT_FLOAT_EQ(output[2], -100.0f);
    EXPECT_FLOAT_EQ(output[3], 0.0f);
    EXPECT_FALSE(NormalizeRgbPixel(rgb, mean, norm, 2, output));
}
