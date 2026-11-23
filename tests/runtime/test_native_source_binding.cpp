#include "composition/source_binding.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
using namespace humanvision::runtime;
namespace {
// Exercise the real source-owned copy boundary without a production test API.
template<class Tag, typename Tag::type Member> struct SourceMemberAccess {
    friend typename Tag::type SourceMember(Tag) { return Member; }
};
struct ConfigMember {using type=OwnedSourceConfig NativeSourceBinding::*;friend type SourceMember(ConfigMember);};
template struct SourceMemberAccess<ConfigMember,&NativeSourceBinding::config_>;
struct ClockMember {using type=InputClockMapping NativeSourceBinding::*;friend type SourceMember(ClockMember);};
template struct SourceMemberAccess<ClockMember,&NativeSourceBinding::mapping_>;
struct PixelsMember {using type=std::vector<uint8_t> NativeSourceBinding::*;friend type SourceMember(PixelsMember);};
template struct SourceMemberAccess<PixelsMember,&NativeSourceBinding::pixels_>;
struct PreviewMember {using type=PreviewMailbox NativeSourceBinding::*;friend type SourceMember(PreviewMember);};
template struct SourceMemberAccess<PreviewMember,&NativeSourceBinding::preview_>;
struct PublishMember {using type=void(NativeSourceBinding::*)(const HV_InputFrameInfo&,uint64_t);friend type SourceMember(PublishMember);};
template struct SourceMemberAccess<PublishMember,&NativeSourceBinding::PublishCpuPreview>;
struct CpuPreview {
    NativeSourceBinding source;
    HV_InputFrameInfo input{sizeof(input),1};
    CpuPreview() {
        auto& config=source.*SourceMember(ConfigMember{});
        config.source_id=77;config.flags=HV_NATIVE_SOURCE_LIVE_PREVIEW;
        source.*SourceMember(ClockMember{})=InputClockMapping{42,1000,0,true};
        (source.*SourceMember(PixelsMember{})).resize(32);
        input.width=4;input.height=2;input.stride_bytes=16;input.rgba_bytes=32;
        input.generation=2;input.clock_id=42;input.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;
        input.row_origin=HV_INPUT_ROW_TOP_LEFT;
    }
    PreviewMailbox& Mailbox(){return source.*SourceMember(PreviewMember{});}
    void Publish(uint64_t sequence) {
        input.sequence=sequence;input.decoded_timestamp_us=static_cast<int64_t>(sequence*33333);
        auto& pixels=source.*SourceMember(PixelsMember{});
        std::fill(pixels.begin(),pixels.end(),static_cast<uint8_t>(sequence));
        (source.*SourceMember(PublishMember{}))(input,3);
    }
};
}
TEST(NativeSourceBinding, ClockPairHasExplicitOffsetAndUncertainty) {
    HV_InputClockInfo c{sizeof(c),1,42,10000,0,1000000,HV_INPUT_CLOCK_NATIVE_MONOTONIC,0};
    InputClockMapping map;ASSERT_TRUE(BuildInputClockMapping(c,20000,20020,map));
    EXPECT_EQ(map.offset_us,10010);EXPECT_EQ(map.uncertainty_us,10);
    HV_InputFrameInfo f{sizeof(f),1};f.clock_id=42;f.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    int64_t output=0;ASSERT_TRUE(map.Map(f,10100,output));EXPECT_EQ(output,20110);
    f.clock_id=43;EXPECT_FALSE(map.Map(f,10100,output));
    EXPECT_FALSE(BuildInputClockMapping(c,20020,20000,map));
}
TEST(NativeSourceBinding, MissingPtsIsNeverUsedAsSensorOrRuntimeClock) {
    HV_InputFrameInfo f{sizeof(f),1};f.clock_id=42;f.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    f.sequence=7;f.generation=3;f.width=320;f.height=240;f.stride_bytes=1280;
    f.received_timestamp_us=-1;f.decoded_timestamp_us=200;f.presentation_timestamp_us=0;f.pts_valid=0;
    InputClockMapping map{42,10000,5,true};const auto e=EnvelopeForNativeFrame(f,map,77,9);
    EXPECT_EQ(e.source_id,77u);EXPECT_EQ(e.generation,3u);EXPECT_EQ(e.config_epoch,9u);EXPECT_EQ(e.frame_id,7);
    EXPECT_TRUE(e.source_timestamp.valid);EXPECT_EQ(e.source_timestamp.microseconds,10200);
    EXPECT_EQ(e.source_timestamp.source,FrameTimeSource::kDecoded);EXPECT_FALSE(e.presentation.valid);
    EXPECT_FALSE(e.received.valid);
    f.pts_valid=1;const auto zero=EnvelopeForNativeFrame(f,map,77,9);
    EXPECT_TRUE(zero.presentation.valid);EXPECT_EQ(zero.presentation.microseconds,0);
    EXPECT_FALSE(zero.presentation.SharesClockWith(zero.source_timestamp));
}
TEST(NativeSourceBinding, UnknownClockAndUnsupportedSourceAreRejected) {
    HV_InputFrameInfo f{sizeof(f),1};f.clock_id=9;f.clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;
    const auto e=EnvelopeForNativeFrame(f,InputClockMapping{8,100,1,true},1,1);
    EXPECT_FALSE(e.source_timestamp.valid);
    HV_RuntimeSourceV1 source{sizeof(source),1};source.kind=HV_NATIVE_SOURCE_RTSP;source.width=640;source.height=480;
    source.url_utf8="file:///fake";OwnedSourceConfig copy;std::string error;
    EXPECT_FALSE(CopySourceConfig(source,copy,error));EXPECT_NE(error.find("RTSP"),std::string::npos);
    source.url_utf8="rtsp://127.0.0.1/test";source.transport_tcp=1;
    EXPECT_TRUE(CopySourceConfig(source,copy,error))<<error;EXPECT_NE(copy.source_id,0u);
}
TEST(NativeSourceBinding, CpuPreviewFollowsReadyFramesWithoutFiveFpsThrottle) {
    CpuPreview preview;ASSERT_TRUE(preview.Mailbox().Configure(32));
    ASSERT_TRUE(preview.Mailbox().AdmitSourceGeneration(77,2,3));
    const auto begin=std::chrono::steady_clock::now();
    for(uint64_t sequence=1;sequence<=8;++sequence)preview.Publish(sequence);
    const auto elapsed=std::chrono::steady_clock::now()-begin;
    ASSERT_LT(elapsed,std::chrono::milliseconds(200))<<"The fixture must exercise consecutive ready frames inside the old throttle window";
    std::array<uint8_t,32> pixels{};HV_RuntimePreviewV1 info{sizeof(info),1};
    ASSERT_EQ(preview.source.CopyPreview(0,pixels.data(),pixels.size(),&info),HV_CONTROL_OK);
    EXPECT_EQ(info.frame_id,8u);EXPECT_EQ(preview.Mailbox().Published(),8u);
    EXPECT_TRUE(std::all_of(pixels.begin(),pixels.end(),[](uint8_t value){return value==8;}));
    EXPECT_NE(preview.source.PreviewDiagnostics().find("live_limit_fps=source"),std::string::npos);
}
TEST(NativeSourceBinding, UnreadCpuPreviewKeepsNewestWithoutOverwritingHeldPixels) {
    CpuPreview preview;ASSERT_TRUE(preview.Mailbox().Configure(32));
    ASSERT_TRUE(preview.Mailbox().AdmitSourceGeneration(77,2,3));
    preview.Publish(1);auto held=preview.Mailbox().AcquireLatest(0);ASSERT_TRUE(held);
    for(uint64_t sequence=2;sequence<=8;++sequence)preview.Publish(sequence);
    std::array<uint8_t,32> pixels{};HV_RuntimePreviewV1 info{sizeof(info),1};
    ASSERT_EQ(preview.source.CopyPreview(1,pixels.data(),pixels.size(),&info),HV_CONTROL_OK);
    EXPECT_EQ(info.frame_id,8u);EXPECT_EQ(held.Info().frame_id,1u);
    EXPECT_EQ(held.Data()[0],1u);EXPECT_EQ(pixels.front(),8u);EXPECT_FALSE(preview.Mailbox().HasInFlight());
}
