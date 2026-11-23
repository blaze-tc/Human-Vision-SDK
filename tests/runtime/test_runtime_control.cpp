#include "humanvision/humanvision_runtime_control.h"
#include "test_support.h"
#include "composition/runtime_controller.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <future>

namespace {
HV_RuntimeControlApiV1 Api() {
    HV_RuntimeControlApiV1 api{sizeof(api),HV_RUNTIME_CONTROL_VERSION};
    EXPECT_EQ(HV_QueryRuntimeControlApiV1(1,&api),HV_CONTROL_OK);
    return api;
}
HV_RuntimeControllerConfigV1 Config(const char* root=HV_TEST_PROJECT_ROOT) {
    return {sizeof(HV_RuntimeControllerConfigV1),1,
        {sizeof(HV_RuntimeConfigV1),HV_API_VERSION_040,root,"cpu",1,0},0,1000};
}
HV_RuntimeRequestStatusV1 Wait(const HV_RuntimeControlApiV1& api,HV_RuntimeControllerHandle h,uint64_t id) {
    HV_RuntimeRequestStatusV1 s{sizeof(s),1};
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    do {
        EXPECT_EQ(api.GetRequestStatus(h,id,&s),0);
        if(s.status>=HV_REQUEST_SUCCEEDED)return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(std::chrono::steady_clock::now()<end);
    ADD_FAILURE()<<"Control request timed out";return s;
}
void Close(const HV_RuntimeControlApiV1& api,HV_RuntimeControllerHandle h) {
    uint64_t id=0;ASSERT_EQ(api.RequestClose(h,&id),0);Wait(api,h,id);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(api.TryRelease(h)==HV_CONTROL_BUSY) {
        ASSERT_LT(std::chrono::steady_clock::now(),end);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}
TEST(RuntimeControl, RejectsWrongSizeAndVersionWithoutWritingTail) {
    HV_RuntimeControlApiV1 api{sizeof(api),1};
    EXPECT_EQ(HV_QueryRuntimeControlApiV1(99,&api),HV_ERR_INVALID_ARGUMENT);
    api.struct_size=8;EXPECT_EQ(HV_QueryRuntimeControlApiV1(1,&api),HV_ERR_INVALID_ARGUMENT);
}
TEST(RuntimeControl, SourceProgressAndLivePreviewAppendToTheExistingTwentyOneEntryTable) {
    struct ExtendedQuery {
        uint32_t size,version;
        void* preserved[21];
        void* source_progress;
        void* attach_live;
    } query{sizeof(ExtendedQuery),1};
    ASSERT_EQ(HV_QueryRuntimeControlApiV1(1,reinterpret_cast<HV_RuntimeControlApiV1*>(&query)),HV_CONTROL_OK);
    ASSERT_NE(query.source_progress,nullptr)<<"Source progress must not require a model result or native analysis claim";
    ASSERT_NE(query.attach_live,nullptr)<<"A retained live consumer must attach to the existing decoder";
    EXPECT_EQ(query.size,sizeof(query));
}
TEST(RuntimeControl, OldTwentyOneEntryQueryKeepsCancellationWithoutWritingSourceTail) {
    HV_RuntimeControlApiV1 api{};api.struct_size=uint32_t(offsetof(HV_RuntimeControlApiV1,GetSourceProgressV1));api.api_version=1;
    auto sentinel=reinterpret_cast<decltype(api.GetSourceProgressV1)>(uintptr_t(0x1234));api.GetSourceProgressV1=sentinel;
    ASSERT_EQ(HV_QueryRuntimeControlApiV1(1,&api),HV_CONTROL_OK);
    EXPECT_EQ(api.struct_size,176u);EXPECT_NE(api.CancelSnapshot,nullptr);EXPECT_EQ(api.GetSourceProgressV1,sentinel);
}
TEST(RuntimeControl, CreateDeferredDoesNotLoadMissingModelsOrDevices) {
    const auto api=Api();auto config=Config("Z:/missing-runtime-no-io");
    HV_RuntimeControllerHandle h=nullptr;char error[512]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0)<<error;
    HV_RuntimeStatsV3 stats{sizeof(stats),3};ASSERT_EQ(api.GetStatsV3(h,&stats),0);
    EXPECT_EQ(stats.state,HV_CONTROL_CREATED);EXPECT_EQ(stats.native_input_frames,0u);
    EXPECT_EQ(api.TryRelease(h),HV_CONTROL_BUSY);
    Close(api,h);
}
TEST(RuntimeControl, MissingModelFailureIsAsynchronousAndActionable) {
    const auto api=Api();auto config=Config("Z:/missing-runtime-no-io");
    HV_RuntimeControllerHandle h=nullptr;char error[512]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    uint64_t id=0;ASSERT_EQ(api.RequestStart(h,&id),0);
    const auto result=Wait(api,h,id);EXPECT_EQ(result.status,HV_REQUEST_FAILED);
    EXPECT_EQ(result.state,HV_CONTROL_ERROR);EXPECT_NE(result.message[0],0);
    Close(api,h);
}
TEST(RuntimeControl, CloseBeforeStartAndRepeatedCloseAreSafe) {
    const auto api=Api();auto config=Config();HV_RuntimeControllerHandle h=nullptr;char error[256]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    uint64_t first=0,second=0;ASSERT_EQ(api.RequestClose(h,&first),0);
    ASSERT_EQ(api.RequestClose(h,&second),0);EXPECT_EQ(first,second);
    Wait(api,h,first);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    int result;while((result=api.TryRelease(h))==HV_CONTROL_BUSY&&std::chrono::steady_clock::now()<end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(result,0);
}
TEST(RuntimeControl, OneHundredDeferredLifecyclesRetireWithoutUnityPump) {
    const auto api=Api();auto config=Config();
    for(int i=0;i<100;++i) {
        HV_RuntimeControllerHandle h=nullptr;char error[256]{};
        ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
        Close(api,h);
    }
}
TEST(RuntimeControl, RealCpuPauseResumeInvalidatesHistoryAndEpoch) {
    const auto api=Api();auto config=Config();HV_RuntimeControllerHandle h=nullptr;char error[512]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    struct Cleanup{const HV_RuntimeControlApiV1& api;HV_RuntimeControllerHandle h;~Cleanup(){Close(api,h);}} cleanup{api,h};
    uint64_t id=0;ASSERT_EQ(api.RequestStart(h,&id),0);ASSERT_EQ(Wait(api,h,id).status,HV_REQUEST_SUCCEEDED);
    auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,42,HV_RuntimeClockUs());
    ASSERT_EQ(api.Submit(h,&frame),0);
    HV_RuntimeStatsV3 before{sizeof(before),3};const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    do {ASSERT_EQ(api.GetStatsV3(h,&before),0);if(before.runtime.fresh_observation_frames)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(std::chrono::steady_clock::now()<end);
    ASSERT_GT(before.runtime.fresh_observation_frames,0u);
    ASSERT_EQ(api.RequestPause(h,&id),0);ASSERT_EQ(Wait(api,h,id).state,HV_CONTROL_PAUSED);
    EXPECT_EQ(api.Submit(h,&frame),HV_ERR_NOT_INITIALIZED);
    ASSERT_EQ(api.RequestResume(h,&id),0);ASSERT_EQ(Wait(api,h,id).state,HV_CONTROL_RUNNING);
    HV_RuntimeStatsV3 after{sizeof(after),3};ASSERT_EQ(api.GetStatsV3(h,&after),0);
    EXPECT_GT(after.config_epoch,before.config_epoch);EXPECT_EQ(after.runtime.fresh_observation_frames,0u);
}
TEST(RuntimeControl, CloseDuringBlockedStartTimesOutWithoutReleasingOwner) {
    auto config=Config();config.retirement_timeout_ms=20;
    std::promise<void> entered,unblock;auto gate=unblock.get_future().share();std::string error;
    auto controller=humanvision::runtime::RuntimeController::Create(config,error,
        [&](const humanvision::runtime::OwnedRuntimeConfig&,humanvision::runtime::GpuConsumerSource*,std::shared_ptr<humanvision::runtime::ObservationJournal>,std::string& message){
            entered.set_value();gate.wait();message="Injected vendor initialization failure";
            return std::unique_ptr<humanvision::runtime::RuntimeSession>{};});
    ASSERT_TRUE(controller)<<error;
    uint64_t start=0,close=0;ASSERT_EQ(controller->Request(humanvision::runtime::RuntimeController::Operation::Start,&start),0);
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(5)),std::future_status::ready);
    EXPECT_EQ(controller->Request(humanvision::runtime::RuntimeController::Operation::Close,&close),0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    HV_RuntimeRequestStatusV1 status{sizeof(status),1};EXPECT_EQ(controller->RequestStatus(close,&status),0);
    EXPECT_EQ(status.state,HV_CONTROL_ERROR_RETIRING);EXPECT_EQ(status.status,HV_REQUEST_TIMED_OUT);
    EXPECT_FALSE(controller->IsRetired());
    EXPECT_NE(std::string(status.message).find("resources remain owned"),std::string::npos);
    unblock.set_value();
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller->IsRetired()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(controller->IsRetired());
}
TEST(RuntimeControl, IndependentRequiredNeverSilentlyUsesUnityInput) {
    const auto api=Api();auto config=Config();config.execution_policy=HV_EXECUTION_INDEPENDENT_REQUIRED;
    HV_RuntimeControllerHandle h=nullptr;char error[256]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    uint64_t id=0;ASSERT_EQ(api.RequestStart(h,&id),0);const auto result=Wait(api,h,id);
    EXPECT_EQ(result.status,HV_REQUEST_FAILED);EXPECT_NE(std::string(result.message).find("native source"),std::string::npos);
    Close(api,h);
}
TEST(RuntimeControl, StopQueuedBehindBlockedStartRetainsOwnerUntilReturn) {
    auto config=Config();config.retirement_timeout_ms=20;
    std::promise<void> entered,unblock;auto gate=unblock.get_future().share();std::string error;
    auto controller=humanvision::runtime::RuntimeController::Create(config,error,
        [&](const humanvision::runtime::OwnedRuntimeConfig&,humanvision::runtime::GpuConsumerSource*,std::shared_ptr<humanvision::runtime::ObservationJournal>,std::string& message){
            entered.set_value();gate.wait();message="Injected vendor failure";
            return std::unique_ptr<humanvision::runtime::RuntimeSession>{};});
    ASSERT_TRUE(controller);uint64_t start=0,stop=0,close=0;
    EXPECT_EQ(controller->Request(humanvision::runtime::RuntimeController::Operation::Start,&start),0);
    EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(5)),std::future_status::ready);
    EXPECT_EQ(controller->Request(humanvision::runtime::RuntimeController::Operation::Stop,&stop),0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    HV_RuntimeRequestStatusV1 status{sizeof(status),1};EXPECT_EQ(controller->RequestStatus(stop,&status),0);
    EXPECT_EQ(status.state,HV_CONTROL_ERROR_RETIRING);EXPECT_EQ(status.status,HV_REQUEST_TIMED_OUT);
    EXPECT_FALSE(controller->IsRetired());unblock.set_value();
    EXPECT_EQ(controller->Request(humanvision::runtime::RuntimeController::Operation::Close,&close),0);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller->IsRetired()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(controller->IsRetired());
}
TEST(RuntimeControl, NativeSourceRejectsSecondUnityProducer) {
    const auto input=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/input-native/windows/Release/humanvision_input.dll";
    if(!std::filesystem::exists(input))GTEST_SKIP()<<"Build the independent Input DLL before this integration test";
    const auto library=input.u8string();const auto api=Api();auto config=Config();config.execution_policy=HV_EXECUTION_INDEPENDENT_REQUIRED;
    HV_RuntimeControllerHandle h=nullptr;char error[256]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    struct Cleanup{const HV_RuntimeControlApiV1& api;HV_RuntimeControllerHandle h;~Cleanup(){Close(api,h);}} cleanup{api,h};
    HV_RuntimeSourceV1 source{sizeof(source),1};source.kind=HV_NATIVE_SOURCE_RTSP;source.library_utf8=library.c_str();
    source.url_utf8="rtsp://127.0.0.1:1/controller-unavailable-fixture";source.source_id=900;source.width=320;source.height=240;
    source.timeout_ms=100;source.reconnect_delay_ms=50;source.transport_tcp=1;
    uint64_t id=0;ASSERT_EQ(api.RequestSource(h,&source,&id),0);ASSERT_EQ(Wait(api,h,id).status,HV_REQUEST_SUCCEEDED);
    ASSERT_EQ(api.RequestStart(h,&id),0);const auto started=Wait(api,h,id);ASSERT_EQ(started.status,HV_REQUEST_SUCCEEDED)<<started.message;
    auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,10,HV_RuntimeClockUs());
    EXPECT_EQ(api.Submit(h,&frame),HV_ERR_INVALID_ARGUMENT);
    HV_RuntimeCapabilitiesV1 capabilities{sizeof(capabilities),1};ASSERT_EQ(api.GetCapabilities(h,&capabilities),0);
    EXPECT_EQ(capabilities.capability_level,HV_CAPABILITY_INDEPENDENT_PIPELINE);
    EXPECT_STREQ(capabilities.source,"native-rtsp");EXPECT_EQ(capabilities.uses_gpu_image_service,0u);
}
TEST(RuntimeControl, JournalCursorSurvivesRealCpuPauseResumeWithIndependentReaders) {
    const auto api=Api();auto config=Config();HV_RuntimeControllerHandle h=nullptr;char error[512]{};
    ASSERT_EQ(api.CreateDeferred(&config,&h,error,sizeof(error)),0);
    struct Cleanup{const HV_RuntimeControlApiV1& api;HV_RuntimeControllerHandle h;~Cleanup(){Close(api,h);}} cleanup{api,h};
    uint64_t request=0;ASSERT_EQ(api.RequestStart(h,&request),0);
    ASSERT_EQ(Wait(api,h,request).status,HV_REQUEST_SUCCEEDED);
    HV_RuntimeStatsV3 before{sizeof(before),3};ASSERT_EQ(api.GetStatsV3(h,&before),0);
    auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
    auto observations=std::make_unique<HV_RuntimeObservationV1[]>(64);
    HV_RuntimeObservationReadV1 read{sizeof(read),1};uint64_t cursor=0;int64_t frame_id=0;
    bool real_body=false;
    const auto input_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    auto next_submit=std::chrono::steady_clock::now();
    // These100+ entries are produced by the real CPU pipeline, not inserted into
    // the journal. The source is paced above the profile's30FPS admission period.
    while(cursor<100&&std::chrono::steady_clock::now()<input_deadline) {
        std::this_thread::sleep_until(next_submit);
        const auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,++frame_id,HV_RuntimeClockUs());
        ASSERT_EQ(api.Submit(h,&frame),0);next_submit=std::chrono::steady_clock::now()+std::chrono::milliseconds(35);
        const auto completion_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do {
            read={sizeof(read),1};ASSERT_EQ(api.ReadObservations(h,cursor,observations.get(),64,&read),0);
            if(read.written) {
                for(uint32_t i=0;i<read.written;++i)real_body|=observations[i].kind==0&&observations[i].body_count>0;
                cursor=read.next_cursor;break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }while(std::chrono::steady_clock::now()<completion_deadline);
    }
    ASSERT_GE(cursor,100u);ASSERT_TRUE(real_body);
    read={sizeof(read),1};ASSERT_EQ(api.ReadObservations(h,0,observations.get(),1,&read),0);
    const auto lost_before=read.lost_count;ASSERT_GT(lost_before,0u);
    ASSERT_EQ(api.RequestPause(h,&request),0);ASSERT_EQ(Wait(api,h,request).state,HV_CONTROL_PAUSED);
    read={sizeof(read),1};EXPECT_EQ(api.ReadObservations(h,0,observations.get(),1,&read),0);
    EXPECT_GT(read.written,0u); // Retained history can be read without an active Session.
    ASSERT_EQ(api.RequestResume(h,&request),0);ASSERT_EQ(Wait(api,h,request).state,HV_CONTROL_RUNNING);
    HV_RuntimeStatsV3 after{sizeof(after),3};ASSERT_EQ(api.GetStatsV3(h,&after),0);
    ASSERT_GT(after.config_epoch,before.config_epoch);
    const auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,10000,HV_RuntimeClockUs());
    ASSERT_EQ(api.Submit(h,&frame),0);
    uint64_t second_cursor=100;bool first_saw_new=false,second_saw_new=false;
    const auto resume_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        for(int reader=0;reader<2;++reader) {
            auto& position=reader?second_cursor:cursor;
            read={sizeof(read),1};ASSERT_EQ(api.ReadObservations(h,position,observations.get(),64,&read),0);
            if(read.written) {
                EXPECT_GT(read.next_cursor,position);
                for(uint32_t i=0;i<read.written;++i)if(observations[i].config_epoch==after.config_epoch) {
                    EXPECT_GT(observations[i].sequence,100u);
                    if(reader)second_saw_new=true;else first_saw_new=true;
                }
                position=read.next_cursor;
            }
        }
        if(first_saw_new&&second_saw_new)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(std::chrono::steady_clock::now()<resume_deadline);
    EXPECT_TRUE(first_saw_new);EXPECT_TRUE(second_saw_new);
    read={sizeof(read),1};ASSERT_EQ(api.ReadObservations(h,0,observations.get(),1,&read),0);
    EXPECT_GE(read.lost_count,lost_before);
    ASSERT_EQ(api.GetStatsV3(h,&after),0);EXPECT_GE(after.journal_overruns,lost_before+read.lost_count);
}
