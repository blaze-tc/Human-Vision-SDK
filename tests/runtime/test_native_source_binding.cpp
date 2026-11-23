#include "composition/source_binding.h"
#include <gtest/gtest.h>
using namespace humanvision::runtime;
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
