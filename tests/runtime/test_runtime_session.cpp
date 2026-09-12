#include "humanvision/humanvision_v2.h"
#include "test_support.h"
#include <gtest/gtest.h>
#include <thread>

TEST(RuntimeSession, SemanticProfileProducesAtomicCanonicalSnapshotAndInvalidatesRevision) {
 HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,HV_TEST_PROJECT_ROOT,"cpu",1,0};
 HV_RuntimeHandle handle=nullptr;char error[1024]{};
 ASSERT_EQ(HV_RuntimeCreate(&config,&handle,error,sizeof(error)),HV_OK)<<error;
 struct Cleanup{HV_RuntimeHandle handle;~Cleanup(){HV_RuntimeDestroy(handle);}} cleanup{handle};
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,42,1000000);
 ASSERT_EQ(HV_RuntimeSubmit(handle,&frame),HV_OK);
 HV_CanonicalBodyV1 bodies[8]{};uint32_t count=0;
 HV_RuntimeStatsV1 stats{};stats.struct_size=sizeof(stats);stats.api_version=HV_API_VERSION_040;
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
 while(stats.body_sequence==0&&std::chrono::steady_clock::now()<end){ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 ASSERT_EQ(count,1u);EXPECT_EQ(stats.source_frame_id,42);EXPECT_EQ(bodies[0].observation_timestamp_us,1000000);
 EXPECT_TRUE(bodies[0].joints[HV_CANONICAL_NOSE].valid);
 ASSERT_EQ(HV_RuntimeCopy(handle,1300000,bodies,8,&count,&stats),HV_OK);EXPECT_EQ(count,0u);
 HV_Rect region{0,0,.5F,1};ASSERT_EQ(HV_RuntimeSetRegions(handle,&region,1,1),HV_OK);
 ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);EXPECT_EQ(count,0u);EXPECT_EQ(stats.region_revision,1);
 EXPECT_EQ(HV_RuntimeSetRegions(handle,&region,1,1),HV_ERR_INVALID_ARGUMENT);
}
TEST(RuntimeSession, MissingSemanticProfileFailsWithActionableError) {
 HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,HV_TEST_PROJECT_ROOT,"missing",1,0};HV_RuntimeHandle handle=nullptr;char error[1024]{};
 EXPECT_NE(HV_RuntimeCreate(&config,&handle,error,sizeof(error)),HV_OK);EXPECT_EQ(handle,nullptr);EXPECT_NE(std::string(error).find("Profile missing"),std::string::npos);
}
