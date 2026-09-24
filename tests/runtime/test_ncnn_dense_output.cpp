#include "plugins/backend/ncnn/ncnn_dense_output.h"
#include <gtest/gtest.h>

using humanvision::runtime::ncnn_backend::DenseOutputLayout;
using humanvision::runtime::ncnn_backend::DescribeDenseOutput;
using humanvision::runtime::ncnn_backend::CompactDenseFp32;
using humanvision::runtime::ncnn_backend::ValidateDenseDownload;

TEST(NcnnDenseOutput, LogicalByteLimitAcceptsAlignedPhysicalChannelStride) {
    DenseOutputLayout layout;
    ASSERT_TRUE(DescribeDenseOutput(3, 3, 1, 1, 2, 4, 24, layout));
    EXPECT_EQ(layout.logical_bytes, 24u);
    EXPECT_EQ(layout.storage_bytes, 32u);
    EXPECT_TRUE(ValidateDenseDownload(layout, 8));
    EXPECT_FALSE(ValidateDenseDownload(layout, 7));
    EXPECT_FALSE(DescribeDenseOutput(3, 3, 1, 1, 2, 100, 24, layout));
}

TEST(NcnnDenseOutput, CopiesOnlyLogicalValuesFromPaddedThreeDimensionalChannels) {
    DenseOutputLayout layout;
    ASSERT_TRUE(DescribeDenseOutput(3, 3, 1, 1, 2, 4, 64, layout));
    EXPECT_EQ(layout.rank, 3u);
    EXPECT_EQ(layout.dimensions[0], 2);
    EXPECT_EQ(layout.dimensions[1], 1);
    EXPECT_EQ(layout.dimensions[2], 3);
    EXPECT_EQ(layout.logical_bytes, 6u * sizeof(float));
    const float padded[] = {1, 2, 3, -99, 4, 5, 6, -99};
    float dense[6]{};
    ASSERT_TRUE(CompactDenseFp32(padded, layout, dense, 6));
    EXPECT_EQ(std::vector<float>(dense, dense + 6),
              (std::vector<float>{1, 2, 3, 4, 5, 6}));
}

TEST(NcnnDenseOutput, PreservesDepthAndRejectsOverLimitOrShortStride) {
    DenseOutputLayout layout;
    ASSERT_TRUE(DescribeDenseOutput(4, 3, 1, 2, 2, 8, 64, layout));
    EXPECT_EQ(layout.rank, 4u);
    EXPECT_EQ(layout.dimensions[0], 2);
    EXPECT_EQ(layout.dimensions[1], 2);
    EXPECT_EQ(layout.dimensions[2], 1);
    EXPECT_EQ(layout.dimensions[3], 3);
    EXPECT_EQ(layout.logical_bytes, 12u * sizeof(float));
    const float padded[] = {1, 2, 3, 4, 5, 6, -99, -99,
                            7, 8, 9, 10, 11, 12, -99, -99};
    float dense[12]{};
    ASSERT_TRUE(CompactDenseFp32(padded, layout, dense, 12));
    EXPECT_EQ(dense[5], 6);
    EXPECT_EQ(dense[6], 7);
    EXPECT_EQ(dense[11], 12);
    EXPECT_FALSE(DescribeDenseOutput(4, 3, 1, 2, 2, 8, 40, layout));
    EXPECT_FALSE(DescribeDenseOutput(4, 3, 1, 2, 2, 5, 64, layout));
}
