#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include "humanvision/humanvision_rtsp.h"

namespace {
struct PublishedExport { const char* name; WORD ordinal; };
// Read from the published preview.17 PE at 69be01b (SHA-256 72e2ffdb...).
// Its FFmpeg-enabled public SDK has 39 exports; the 35-export no-RTSP
// development build is not the compatibility baseline for this artifact.
constexpr PublishedExport kPublished[] = {
    {"HV_AndroidGpuCancelUnissuedFrameCopy",1}, {"HV_AndroidGpuPollFrameCopy",2},
    {"HV_AndroidGpuPollSourceRetirement",3}, {"HV_AndroidGpuRetireSourceCopies",4},
    {"HV_Create",5}, {"HV_Destroy",6}, {"HV_GetAndroidGpuRenderEventAndDataFunction",7},
    {"HV_GetBodies",8}, {"HV_GetBodyCount",9}, {"HV_GetHandJoints",10},
    {"HV_GetLastError",11}, {"HV_GetLatestResultMeta",12}, {"HV_GetRegionAssignments",13},
    {"HV_GetStats",14}, {"HV_GetVersionString",15}, {"HV_Reconfigure",16},
    {"HV_RtspClose",17}, {"HV_RtspCopyFrame",18}, {"HV_RtspOpen",19}, {"HV_RtspState",20},
    {"HV_RuntimeBeginAndroidGpuSourceLease",21}, {"HV_RuntimeClockUs",22},
    {"HV_RuntimeCopy",23}, {"HV_RuntimeCreate",24}, {"HV_RuntimeDestroy",25},
    {"HV_RuntimeEndAndroidGpuSourceLease",26}, {"HV_RuntimeGetAndroidGpuBridgeStatus",27},
    {"HV_RuntimeGetDiagnostics",28}, {"HV_RuntimeGetError",29}, {"HV_RuntimeGetStatsV2",30},
    {"HV_RuntimePrepareAndroidGpuFrame",31}, {"HV_RuntimePrepareAndroidGpuFrameCopy",32},
    {"HV_RuntimeRecordSourceFrameV2",33}, {"HV_RuntimeRetireAndroidGpuSourceCopies",34},
    {"HV_RuntimeSetCaptureProvenanceV2",35}, {"HV_RuntimeSetRegions",36},
    {"HV_RuntimeSubmit",37}, {"HV_SetRegions",38}, {"HV_SubmitFrame",39}
};
}

TEST(WindowsPublicExports, PublishedNamesAndOrdinalsRemainCallable) {
    HMODULE module = GetModuleHandleW(L"humanvision.dll");
    ASSERT_NE(module,nullptr);
    for(const auto& entry:kPublished) {
        auto named=GetProcAddress(module,entry.name);
        ASSERT_NE(named,nullptr)<<entry.name;
        EXPECT_EQ(named,GetProcAddress(module,MAKEINTRESOURCEA(entry.ordinal)))<<entry.name;
    }
    auto control=GetProcAddress(module,"HV_QueryRuntimeControlApiV1");
    ASSERT_NE(control,nullptr);
    EXPECT_EQ(control,GetProcAddress(module,MAKEINTRESOURCEA(40)));
}

TEST(WindowsPublicExports, LegacyRtspEntryPointsRetainTheirErrorContract) {
    HMODULE module=GetModuleHandleW(L"humanvision.dll");ASSERT_NE(module,nullptr);
    auto open=reinterpret_cast<decltype(&HV_RtspOpen)>(GetProcAddress(module,"HV_RtspOpen"));
    auto copy=reinterpret_cast<decltype(&HV_RtspCopyFrame)>(GetProcAddress(module,"HV_RtspCopyFrame"));
    auto state=reinterpret_cast<decltype(&HV_RtspState)>(GetProcAddress(module,"HV_RtspState"));
    auto close=reinterpret_cast<decltype(&HV_RtspClose)>(GetProcAddress(module,"HV_RtspClose"));
    ASSERT_NE(open,nullptr);ASSERT_NE(copy,nullptr);ASSERT_NE(state,nullptr);ASSERT_NE(close,nullptr);
    EXPECT_EQ(open(nullptr,1920,1080,1,100),nullptr);
    EXPECT_EQ(open("file:///not-an-rtsp-source",1920,1080,1,100),nullptr);
    EXPECT_EQ(state(nullptr),4);
    EXPECT_EQ(copy(nullptr,0,nullptr,0,nullptr,nullptr,nullptr,nullptr),-1);
    close(nullptr);
}
