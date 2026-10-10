#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_rgb_preprocess.h"
#include <vector>
#include <array>
#include <algorithm>

using humanvision::runtime::yolo::TryFastRgbPreprocess;
void BeginNativeAllocationProbe() noexcept;
std::size_t EndNativeAllocationProbe() noexcept;

TEST(YoloRgbPreprocess, AllBytesMatchIdentityAndTwoByTwoOracleForFourFormats) {
    for (auto format : {HV_PIXEL_RGB24, HV_PIXEL_BGR24, HV_PIXEL_RGBA32, HV_PIXEL_BGRA32}) {
        const int channels = format == HV_PIXEL_RGB24 || format == HV_PIXEL_BGR24 ? 3 : 4;
        const bool rgb = format == HV_PIXEL_RGB24 || format == HV_PIXEL_RGBA32;
        for (int scale : {1, 2}) {
            const int width = 512 * scale, height = 288 * scale, stride = width * channels + 19;
            std::vector<uint8_t> source(size_t(stride) * height, 239), expected(512 * 288 * 3);
            const auto value = [](int x, int y, int c) { return uint8_t((x * 19 + y * 37 + c * 83) & 255); };
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                auto* p = source.data() + size_t(y) * stride + x * channels;
                for (int c = 0; c < 3; ++c) p[rgb ? c : 2 - c] = value(x, y, c);
                if (channels == 4) p[3] = uint8_t(x + y);
            }
            for (int y = 0; y < 288; ++y) for (int x = 0; x < 512; ++x) for (int c = 0; c < 3; ++c) {
                const int sum = scale == 1 ? value(x, y, c) : value(2*x, 2*y, c) +
                    value(2*x+1, 2*y, c) + value(2*x, 2*y+1, c) + value(2*x+1, 2*y+1, c);
                expected[(y * 512 + x) * 3 + c] = uint8_t(scale == 1 ? sum : (sum + 2) / 4);
            }
            std::array<uint8_t, 512 * 288 * 3> actual{};
            HV_VideoFrame frame{sizeof(frame), width, height, stride, format, 7, 13,
                                source.data(), int32_t(source.size())};
            BeginNativeAllocationProbe();
            const bool accepted = TryFastRgbPreprocess(frame, actual.data());
            const auto allocations = EndNativeAllocationProbe();
            ASSERT_TRUE(accepted);
            EXPECT_EQ(allocations, 0u);
            EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin()));
        }
    }
}

TEST(YoloRgbPreprocess, UnsupportedGeometryAndInvalidBuffersDoNotWriteOutput) {
    std::vector<uint8_t> source(512 * 288 * 3), output(source.size(), 197);
    HV_VideoFrame frame{sizeof(frame),512,288,512*3,HV_PIXEL_RGB24,1,1,source.data(),int32_t(source.size())};
    for (int invalid = 0; invalid < 7; ++invalid) {
        auto candidate = frame;
        if (invalid == 0) candidate.width = 640;
        if (invalid == 1) candidate.height = 287;
        if (invalid == 2) candidate.stride_bytes = 511 * 3;
        if (invalid == 3) candidate.data_bytes--;
        if (invalid == 4) candidate.data = nullptr;
        if (invalid == 5) candidate.pixel_format = static_cast<HV_PixelFormat>(999);
        if (invalid == 6) candidate.struct_size--;
        EXPECT_FALSE(TryFastRgbPreprocess(candidate, output.data()));
        EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](uint8_t v) { return v == 197; }));
    }
    EXPECT_FALSE(TryFastRgbPreprocess(frame, nullptr));
}

// 720p is the actual RK3588 readback geometry from the supplied field logs.
// At 5:2 the quarter weights alternate; preserve the original two staged floors.
TEST(YoloRgbPreprocess, All720pBytesMatchStagedQuarterWeightOracle) {
    for(auto format:{HV_PIXEL_RGB24,HV_PIXEL_BGR24,HV_PIXEL_RGBA32,HV_PIXEL_BGRA32}) {
        const int channels=format==HV_PIXEL_RGB24||format==HV_PIXEL_BGR24?3:4;
        const bool rgb=format==HV_PIXEL_RGB24||format==HV_PIXEL_RGBA32;
        const int stride=1280*channels+19;
        std::vector<uint8_t> source(size_t(stride)*720),expected(512*288*3),actual(expected.size());
        uint32_t seed=1234567;
        for(auto& value:source){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;value=uint8_t(seed);}
        for(int y=0;y<288;++y)for(int x=0;x<512;++x)for(int c=0;c<3;++c) {
            const int ix=(5*x+1)/2,iy=(5*y+1)/2,wx=x%2?3:1,wy=y%2?3:1,offset=rgb?c:2-c;
            const auto at=[&](int xx,int yy){return int(source[size_t(yy)*stride+xx*channels+offset]);};
            const int a=wx*at(ix,iy)+(4-wx)*at(ix+1,iy);
            const int b=wx*at(ix,iy+1)+(4-wx)*at(ix+1,iy+1);
            expected[(y*512+x)*3+c]=uint8_t(((wy*a)/4+((4-wy)*b)/4+2)/4);
        }
        HV_VideoFrame frame{sizeof(frame),1280,720,stride,format,1,1,source.data(),int32_t(source.size())};
        BeginNativeAllocationProbe();const bool accepted=TryFastRgbPreprocess(frame,actual.data());
        const auto allocations=EndNativeAllocationProbe();ASSERT_TRUE(accepted);EXPECT_EQ(allocations,0u);
        EXPECT_EQ(actual,expected);
    }
}
