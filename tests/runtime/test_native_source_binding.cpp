#include "composition/source_binding.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>
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
struct ApiMember {using type=NativeInputApi NativeSourceBinding::*;friend type SourceMember(ApiMember);};
template struct SourceMemberAccess<ApiMember,&NativeSourceBinding::api_>;
struct HandleMember {using type=HV_InputHandle NativeSourceBinding::*;friend type SourceMember(HandleMember);};
template struct SourceMemberAccess<HandleMember,&NativeSourceBinding::handle_>;
struct RunningMember {using type=std::atomic<bool> NativeSourceBinding::*;friend type SourceMember(RunningMember);};
template struct SourceMemberAccess<RunningMember,&NativeSourceBinding::running_>;
struct FrameMember {using type=std::atomic<uint64_t> NativeSourceBinding::*;friend type SourceMember(FrameMember);};
template struct SourceMemberAccess<FrameMember,&NativeSourceBinding::frame_id_>;
struct BytesMember {using type=std::atomic<uint64_t> NativeSourceBinding::*;friend type SourceMember(BytesMember);};
template struct SourceMemberAccess<BytesMember,&NativeSourceBinding::capture_rgba_bytes_>;
struct WorkerMember {using type=std::thread NativeSourceBinding::*;friend type SourceMember(WorkerMember);};
template struct SourceMemberAccess<WorkerMember,&NativeSourceBinding::preview_worker_>;
struct LoopMember {using type=void(NativeSourceBinding::*)(uint64_t) noexcept;friend type SourceMember(LoopMember);};
template struct SourceMemberAccess<LoopMember,&NativeSourceBinding::PreviewLoop>;
struct EpochMember {using type=uint64_t NativeSourceBinding::*;friend type SourceMember(EpochMember);};
template struct SourceMemberAccess<EpochMember,&NativeSourceBinding::epoch_>;
struct SourceOnlyProgress {
    NativeSourceBinding source;uint64_t generation=2;
    SourceOnlyProgress(){
        auto& config=source.*SourceMember(ConfigMember{});config.source_id=77;
        source.*SourceMember(EpochMember{})=9;
        source.*SourceMember(HandleMember{})=reinterpret_cast<HV_InputHandle>(this);
        (source.*SourceMember(ApiMember{})).source_progress=Read;
    }
    ~SourceOnlyProgress(){source.*SourceMember(HandleMember{})=nullptr;}
    static int HV_INPUT_CALL Read(HV_InputHandle handle,HV_InputSourceProgressV1* out){
        const auto& self=*reinterpret_cast<SourceOnlyProgress*>(handle);
        *out={sizeof(*out),1};out->generation=self.generation;
        out->decoded_frames=120;out->acquired_frames=119;out->live_published_frames=118;
        out->analysis_published_frames=117;out->analysis_claimed_frames=0;out->state=HV_INPUT_STREAMING;
        out->source_width=1920;out->source_height=1080;out->live_width=1280;out->live_height=720;
        out->live_available=out->live_enabled=1;return HV_INPUT_OK;
    }
};
struct SlowCapture {
    NativeSourceBinding source;
    std::atomic<uint64_t> requests{0},copies{0},generation{2};
    const bool slow_copy;
    std::chrono::steady_clock::time_point requested;
    uint64_t requested_generation=2;
    explicit SlowCapture(bool copy) :slow_copy(copy) {
        auto& config=source.*SourceMember(ConfigMember{});config.source_id=77;config.flags=HV_NATIVE_SOURCE_LIVE_PREVIEW;
        source.*SourceMember(ClockMember{})=InputClockMapping{42,1000,0,true};
        auto& api=source.*SourceMember(ApiMember{});api.request_capture=Request;api.copy_capture=Copy;
        source.*SourceMember(HandleMember{})=reinterpret_cast<HV_InputHandle>(this);
        (source.*SourceMember(FrameMember{}))=100;(source.*SourceMember(BytesMember{}))=16;
        Mailbox().AdmitSourceGeneration(77,2,3);
    }
    ~SlowCapture(){source.StopIngestion();source.*SourceMember(HandleMember{})=nullptr;}
    PreviewMailbox& Mailbox(){return source.*SourceMember(PreviewMember{});}
    void Start() {
        (source.*SourceMember(RunningMember{}))=true;
        source.*SourceMember(WorkerMember{})=std::thread([this]{(source.*SourceMember(LoopMember{}))(3);});
    }
    bool WaitForCopies(uint64_t count,int timeout_ms=3000) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        while(Mailbox().Published()<count&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Mailbox().Published()>=count;
    }
    bool WaitForBudget() {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(!Mailbox().BudgetExceeded()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Mailbox().BudgetExceeded();
    }
    static int32_t Request(HV_InputHandle handle,uint64_t* request) {
        auto& self=*reinterpret_cast<SlowCapture*>(handle);
        self.requested=std::chrono::steady_clock::now();self.requested_generation=self.generation.load();
        *request=++self.requests;return HV_INPUT_OK;
    }
    static int32_t Copy(HV_InputHandle handle,uint64_t request,void* pixels,uint32_t capacity,HV_InputFrameInfo* info) {
        auto& self=*reinterpret_cast<SlowCapture*>(handle);
        if(!self.slow_copy&&request==1&&std::chrono::steady_clock::now()-self.requested<std::chrono::milliseconds(220))return HV_INPUT_NO_FRAME;
        *info={sizeof(*info),1};info->width=2;info->height=2;info->stride_bytes=8;info->rgba_bytes=16;
        info->sequence=100+request;info->generation=self.requested_generation;info->decoded_timestamp_us=static_cast<int64_t>(info->sequence*33333);
        info->clock_id=42;info->clock_domain=HV_INPUT_CLOCK_NATIVE_MONOTONIC;info->row_origin=HV_INPUT_ROW_TOP_LEFT;
        if(!pixels||capacity<16)return HV_INPUT_BUFFER_TOO_SMALL;
        if(self.slow_copy&&request==1)std::this_thread::sleep_for(std::chrono::milliseconds(25));
        std::memset(pixels,int(request),16);++self.copies;return HV_INPUT_OK;
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
TEST(NativeSourceBinding, SourceOnlyProgressExistsWithoutAnAnalysisClaimOrResult) {
    SourceOnlyProgress fixture;HV_RuntimeSourceProgressV1 progress{sizeof(progress),1};
    ASSERT_EQ(fixture.source.SourceProgress(&progress),HV_CONTROL_OK);
    EXPECT_EQ(progress.source_id,77u);EXPECT_EQ(progress.config_epoch,9u);EXPECT_EQ(progress.generation,2u);
    EXPECT_EQ(progress.decoded_frames,120u);EXPECT_EQ(progress.live_published_frames,118u);
    EXPECT_EQ(progress.analysis_claimed_frames,0u);EXPECT_EQ(progress.source_width,1920u);EXPECT_EQ(progress.live_width,1280u);
    EXPECT_GT(progress.timestamp_us,0);EXPECT_EQ(fixture.source.Progress().frames,0u);
}
TEST(NativeSourceBinding, SourceOnlyReconnectIdentityAdvancesBeforeAnyClaimOrResult) {
    SourceOnlyProgress fixture;HV_RuntimeSourceProgressV1 first{sizeof(first),1},next{sizeof(next),1};
    ASSERT_EQ(fixture.source.SourceProgress(&first),HV_CONTROL_OK);
    fixture.generation=3;ASSERT_EQ(fixture.source.SourceProgress(&next),HV_CONTROL_OK);
    EXPECT_EQ(first.source_id,next.source_id);EXPECT_EQ(first.config_epoch,next.config_epoch);
    EXPECT_EQ(next.generation,3u);EXPECT_EQ(next.analysis_claimed_frames,0u);
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
TEST(NativeSourceBinding, PreviewRecoversAfterOneSlowCpuCopyWithoutRestartingTheSource) {
    SlowCapture capture(true);capture.Start();ASSERT_TRUE(capture.WaitForCopies(1));
    ASSERT_TRUE(capture.Mailbox().BudgetExceeded());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));EXPECT_EQ(capture.requests.load(),1u);
    ASSERT_TRUE(capture.WaitForCopies(2))<<"A single >20ms optional copy must not freeze live preview forever";
    EXPECT_TRUE(capture.Mailbox().BudgetExceeded())<<"Recovery must retain the historical budget breach";
    const auto requests=capture.requests.load();std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(capture.requests.load(),requests)<<"Recovered previews stay limited to one attempt per second";
    ASSERT_TRUE(capture.WaitForCopies(4));EXPECT_EQ(capture.Mailbox().RecoveryCopiesRemaining(),0u);
    ASSERT_TRUE(capture.WaitForCopies(5,600))<<"Three healthy probes must restore the original 200ms request cadence";
}
TEST(NativeSourceBinding, PreviewRecoversAfterOneSlowReadyCaptureAndStopCannotReopenIt) {
    SlowCapture capture(false);capture.Start();ASSERT_TRUE(capture.WaitForCopies(1));
    ASSERT_TRUE(capture.WaitForBudget()); // request-to-ready pressure is recorded after publication.
    ASSERT_TRUE(capture.WaitForCopies(2))<<"A single >200ms request-to-ready delay must not freeze live preview forever";
    ASSERT_TRUE(capture.WaitForCopies(4));EXPECT_EQ(capture.Mailbox().RecoveryCopiesRemaining(),0u);
    ASSERT_TRUE(capture.WaitForCopies(5,600));
    capture.source.StopIngestion();const auto requests=capture.requests.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));EXPECT_EQ(capture.requests.load(),requests);
    std::array<uint8_t,16> pixels{};HV_RuntimePreviewV1 info{sizeof(info),1};
    EXPECT_EQ(capture.source.CopyPreview(0,pixels.data(),pixels.size(),&info),HV_CONTROL_NO_DATA);
}
TEST(NativeSourceBinding, RecoveryPublishesOnlyTheCurrentSourceGeneration) {
    SlowCapture capture(true);capture.Start();ASSERT_TRUE(capture.WaitForCopies(1));
    capture.generation=3;ASSERT_TRUE(capture.Mailbox().AdmitSourceGeneration(77,3,3));
    ASSERT_TRUE(capture.WaitForCopies(2));
    std::array<uint8_t,16> pixels{};HV_RuntimePreviewV1 info{sizeof(info),1};
    ASSERT_EQ(capture.source.CopyPreview(0,pixels.data(),pixels.size(),&info),HV_CONTROL_OK);
    EXPECT_EQ(info.generation,3u);EXPECT_GE(info.frame_id,102u);EXPECT_TRUE(capture.Mailbox().BudgetExceeded());
}
TEST(NativeSourceBinding, StopDuringPreviewCooldownIsPromptAndNeverRequestsAnotherCapture) {
    SlowCapture capture(true);capture.Start();ASSERT_TRUE(capture.WaitForCopies(1));ASSERT_TRUE(capture.WaitForBudget());
    ASSERT_GT(capture.Mailbox().RecoveryCopiesRemaining(),0u);
    const auto started=std::chrono::steady_clock::now();capture.source.StopIngestion();
    EXPECT_LT(std::chrono::steady_clock::now()-started,std::chrono::milliseconds(250));
    const auto requests=capture.requests.load();std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_EQ(capture.requests.load(),requests);EXPECT_FALSE(capture.Mailbox().HasInFlight());
    std::array<uint8_t,16> pixels{};HV_RuntimePreviewV1 info{sizeof(info),1};
    EXPECT_EQ(capture.source.CopyPreview(0,pixels.data(),pixels.size(),&info),HV_CONTROL_NO_DATA);
}
