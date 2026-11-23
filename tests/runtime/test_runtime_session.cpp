#include "humanvision/humanvision_v2.h"
#include "test_support.h"
#include "composition/session.h"
#include "plugins/backend/ort/ort_plugin.h"
#include "plugins/pipeline/yolo/yolo_tensor_pipeline.h"
#include "picosha2/picosha2.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <thread>

namespace {
using namespace humanvision::runtime;
class YoloCpuCandidateSelection : public ::testing::Test {
protected:
 const std::filesystem::path project=HV_TEST_PROJECT_ROOT;
 const std::filesystem::path fixture=project/"out/rknn-validation-20261009";
 // CTest may run the two cases in separate processes at the same time. Each
 // case must own its staged model so copying it cannot invalidate another hash.
 const std::filesystem::path runtime=project/"out"/
     (std::string("yolo-cpu-candidate-tests-")+
      ::testing::UnitTest::GetInstance()->current_test_info()->name());
 std::vector<uint8_t> pixels;
 PluginRegistry registry;
 std::shared_ptr<const RuntimeProfile> profile;
 void SetUp() override {
  pixels=humanvision::test::ReadBytes((fixture/"seven/source.rgb").string());
  ASSERT_EQ(picosha2::hash256_hex_string(pixels),
      "814e663406f6756414768311ea62dfb364cd2bc9e0e6cf72bb1b4422ef8bd3c6");
  const auto model=humanvision::test::ReadBytes((fixture/"recovered-low.onnx").string());
  ASSERT_EQ(picosha2::hash256_hex_string(model),
      "6c3431e00a8dace37c6a6b9546995e8d2a83c15d5fb894cc467e8c5ab5be88b3");
  const auto pack=runtime/"modelpacks/yolov8n-pose-512x288-ort";
  std::filesystem::create_directories(pack);
  std::filesystem::create_directories(runtime/"profiles");
  for(const auto& files:{std::pair{fixture/"recovered-low.onnx",pack/"body.onnx"},
      std::pair{project/"modelpacks/yolov8n-pose-512x288-ort/manifest.json",pack/"manifest.json"},
      std::pair{project/"profiles/android-cpu-nohands.json",runtime/"profiles/android-cpu-nohands.json"}})
   std::filesystem::copy_file(files.first,files.second,std::filesystem::copy_options::overwrite_existing);
  std::string error;
  ASSERT_TRUE(registry.Register(HV_QueryOrtCpuPlugin,error))<<error;
  ASSERT_TRUE(registry.Register(HV_QueryYoloCpuPipelineV1,error))<<error;
  profile=ProfileManager(runtime/"profiles").Resolve("android-cpu-nohands",4,registry,
      ModelPackManager(runtime/"modelpacks"),error);
  ASSERT_TRUE(profile)<<error;
  ASSERT_EQ(profile->max_people,4);
 }
 HV_VideoFrame Frame(int64_t timestamp) const {
  return {sizeof(HV_VideoFrame),1024,576,3072,HV_PIXEL_RGB24,1500,timestamp,
          pixels.data(),int32_t(pixels.size())};
 }
};
}

TEST_F(YoloCpuCandidateSelection, PublicFourRetainsRealSevenCandidatesBeforeTracking) {
 const auto root=profile->body.pack->root.u8string();
 const auto config=detail::BuildPipelineConfig(profile->body,profile->max_people,
     root.c_str(),profile->json.c_str());
 EXPECT_EQ(config.max_bodies,8);
 BackendFactory factory(profile->backends,profile->allow_backend_fallback);
 RuntimeHost worker;std::string error;
 ASSERT_TRUE(worker.Start(profile->body.plugin,config,factory.Services(),error))<<error;
 const auto timestamp=HV_RuntimeClockUs();
 ASSERT_TRUE(worker.Submit(Frame(timestamp),error))<<error;
 HV_ObservationFrameV1 observation{};bool received=false;
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(!received&&std::chrono::steady_clock::now()<deadline){
  received=worker.CopyLatest(observation);
  if(!received)std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 ASSERT_TRUE(received)<<worker.LastError();
 ASSERT_EQ(observation.body_count,7u);
 EXPECT_EQ(observation.source_frame_id,1500);
 EXPECT_EQ(observation.source_timestamp_us,timestamp);
 EXPECT_EQ(factory.SelectionDiagnostics().actual,"CPU");
 BodyServices services;services.Configure(4,nullptr,0,0);
 services.Observe(observation,0,false);
 const auto original=services.Raw();ASSERT_EQ(original.count,4u);
 // Replay the real ONNX observation with all candidates present, changing their
 // admission order. This is selection identity coverage, not an inference FPS test.
 for(int step=1;step<=60;++step){
  auto frame=observation;frame.source_frame_id+=step;frame.source_timestamp_us+=step*33333;
  std::rotate(frame.bodies,frame.bodies+step%7,frame.bodies+7);
  for(uint32_t i=0;i<frame.body_count;++i)for(auto& joint:frame.bodies[i].joints)
   joint.observation_timestamp_us=frame.source_timestamp_us;
  services.Observe(frame,0,false);
  const auto current=services.Raw();ASSERT_EQ(current.count,4u);
  for(uint32_t i=0;i<current.count;++i){
   EXPECT_EQ(current.bodies[i].track_id,original.bodies[i].track_id);
   EXPECT_EQ(current.bodies[i].source_frame_id,frame.source_frame_id);
   EXPECT_FLOAT_EQ(current.bodies[i].bbox_px.x,original.bodies[i].bbox_px.x);
  }
 }
}

TEST_F(YoloCpuCandidateSelection, RuntimePublicFourCanSelectFourRearPeopleFromRealSeven) {
 const auto assets=runtime.u8string();
 HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,assets.c_str(),"android-cpu-nohands",4,0};
 HV_RuntimeHandle handle=nullptr;char error[1024]{};
 ASSERT_EQ(HV_RuntimeCreate(&config,&handle,error,sizeof(error)),HV_OK)<<error;
 struct Cleanup{HV_RuntimeHandle handle;~Cleanup(){HV_RuntimeDestroy(handle);}} cleanup{handle};
 // Literal regions from the independently annotated seven-person source frame:
 // left rear, middle-left rear, middle-right rear, and right rear. The front
 // people have larger boxes, so truncating at the decoder loses requested people.
 const HV_Rect regions[4]{{.15F,0,.09F,1},{.38F,0,.08F,1},{.60F,0,.06F,1},{.82F,0,.10F,1}};
 ASSERT_EQ(HV_RuntimeSetRegions(handle,regions,4,1),HV_OK);
 const auto timestamp=HV_RuntimeClockUs();const auto frame=Frame(timestamp);
 ASSERT_EQ(HV_RuntimeSubmit(handle,&frame),HV_OK);
 HV_CanonicalBodyV1 bodies[8]{};uint32_t count=0;
 HV_RuntimeStatsV1 stats{sizeof(stats),HV_API_VERSION_040};
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(!stats.body_sequence&&std::chrono::steady_clock::now()<deadline){
  ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 ASSERT_NE(stats.body_sequence,0);
 ASSERT_EQ(count,4u);
 EXPECT_EQ(stats.source_frame_id,1500);EXPECT_EQ(stats.region_revision,1);
 for(uint32_t i=0;i<count;++i){
  const auto& body=bodies[i];const auto& region=regions[i];
  EXPECT_EQ(body.region_index,int32_t(i));
  EXPECT_EQ(body.source_frame_id,1500);EXPECT_EQ(body.observation_timestamp_us,timestamp);
  const auto& left=body.joints[HV_CANONICAL_HIP_LEFT];
  const auto& right=body.joints[HV_CANONICAL_HIP_RIGHT];
  ASSERT_TRUE(left.valid);ASSERT_TRUE(right.valid);
  const float x=(left.x_px+right.x_px)*.5F/1024;
  EXPECT_GE(x,region.x);EXPECT_LE(x,region.x+region.width);
 }
}

TEST(RuntimeSession, SemanticProfileProducesAtomicCanonicalSnapshotAndInvalidatesRevision) {
 HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,HV_TEST_PROJECT_ROOT,"cpu",1,0};
 HV_RuntimeHandle handle=nullptr;char error[1024]{};
 ASSERT_EQ(HV_RuntimeCreate(&config,&handle,error,sizeof(error)),HV_OK)<<error;
 struct Cleanup{HV_RuntimeHandle handle;~Cleanup(){HV_RuntimeDestroy(handle);}} cleanup{handle};
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 const int64_t observation_time=HV_RuntimeClockUs();
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,42,observation_time);
 ASSERT_EQ(HV_RuntimeSubmit(handle,&frame),HV_OK);
 HV_CanonicalBodyV1 bodies[8]{};uint32_t count=0;
 HV_RuntimeStatsV1 stats{};stats.struct_size=sizeof(stats);stats.api_version=HV_API_VERSION_040;
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
 while(stats.body_sequence==0&&std::chrono::steady_clock::now()<end){ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 ASSERT_EQ(count,1u);EXPECT_EQ(stats.source_frame_id,42);EXPECT_EQ(bodies[0].observation_timestamp_us,observation_time);
 EXPECT_TRUE(bodies[0].joints[HV_CANONICAL_NOSE].valid);
 HV_RuntimeStatsV2 measured{};measured.struct_size=sizeof(measured);measured.api_version=HV_RUNTIME_STATS_V2_VERSION;
 ASSERT_EQ(HV_RuntimeGetStatsV2(handle,&measured),HV_OK);
 EXPECT_EQ(measured.fresh_observation_frames,1u);
 EXPECT_EQ(measured.source_frame_id,42);
 EXPECT_EQ(measured.capture_timestamp_us,observation_time);
 // Sampling the same observation cannot create another completed inference.
 ASSERT_EQ(HV_RuntimeCopy(handle,HV_RuntimeClockUs(),bodies,8,&count,&stats),HV_OK);
 ASSERT_EQ(HV_RuntimeGetStatsV2(handle,&measured),HV_OK);
 EXPECT_EQ(measured.fresh_observation_frames,1u);EXPECT_GE(measured.output_samples,1u);
 char diagnostics[8192]{};ASSERT_EQ(HV_RuntimeGetDiagnostics(handle,diagnostics,sizeof(diagnostics)),HV_OK);
 const std::string text=diagnostics;
 for(const char* field:{"Profile=cpu","Pipeline=pipeline.topdown","Requested backend=CPU","Actual backend=CPU",
      "Raw observation bodies=1","Tracked bodies=1","Sampled bodies=1","Raw body FPS=","Body pre/infer/post ms=",
      "Result age ms=","Observation period EWMA ms=","Render hold ms=","Sample state=","Dropped input frames=",
      "Dropped body frames=","Hands enabled=true","Detector inference ms=","Detector executions=","Detector FPS=",
      "Pose inference total ms=","Pose person count="})EXPECT_NE(text.find(field),std::string::npos)<<field<<"\n"<<text;
 ASSERT_EQ(HV_RuntimeCopy(handle,observation_time+300000,bodies,8,&count,&stats),HV_OK);ASSERT_EQ(count,1u);
 EXPECT_EQ(bodies[0].source_frame_id,42);EXPECT_EQ(bodies[0].observation_timestamp_us,observation_time);
 ASSERT_EQ(HV_RuntimeCopy(handle,observation_time+500001,bodies,8,&count,&stats),HV_OK);EXPECT_EQ(count,0u);
 HV_Rect region{0,0,.5F,1};ASSERT_EQ(HV_RuntimeSetRegions(handle,&region,1,1),HV_OK);
 ASSERT_EQ(HV_RuntimeCopy(handle,0,bodies,8,&count,&stats),HV_OK);EXPECT_EQ(count,0u);EXPECT_EQ(stats.region_revision,1);
 EXPECT_EQ(HV_RuntimeSetRegions(handle,&region,1,1),HV_ERR_INVALID_ARGUMENT);
}
TEST(RuntimeSession, MissingSemanticProfileFailsWithActionableError) {
 HV_RuntimeConfigV1 config{sizeof(config),HV_API_VERSION_040,HV_TEST_PROJECT_ROOT,"missing",1,0};HV_RuntimeHandle handle=nullptr;char error[1024]{};
 EXPECT_NE(HV_RuntimeCreate(&config,&handle,error,sizeof(error)),HV_OK);EXPECT_EQ(handle,nullptr);EXPECT_NE(std::string(error).find("Profile missing"),std::string::npos);
}

TEST(RuntimeSession, LastCompletionPublishesWithoutPolling) {
 RuntimeSession session;std::string error;
 ASSERT_TRUE(session.Start(HV_TEST_PROJECT_ROOT,"cpu",1,error))<<error;
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,901,HV_RuntimeClockUs());
 ASSERT_TRUE(session.Submit(frame,error))<<error;
 // Diagnostics is read-only. No more input, Copy or Stats call may drive the
 // semantic merge of this last completed inference.
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
 bool published=false;
 while(std::chrono::steady_clock::now()<deadline){
  if(session.Diagnostics().find("Raw observation bodies=1")!=std::string::npos){published=true;break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 ASSERT_TRUE(published)<<session.LastError();
 const auto measured=session.StatsV2();
 EXPECT_EQ(measured.fresh_observation_frames,1u);
 EXPECT_EQ(measured.source_frame_id,901);
}

TEST(RuntimeSession, NoUnityConsumerStillPublishes) {
 RuntimeSession session;std::string error;
 ASSERT_TRUE(session.Start(HV_TEST_PROJECT_ROOT,"cpu",1,error))<<error;
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,902,HV_RuntimeClockUs());
 ASSERT_TRUE(session.Submit(frame,error))<<error;
 // This intentionally supplies no consumer call until the model has completed.
 std::this_thread::sleep_for(std::chrono::seconds(3));
 EXPECT_NE(session.Diagnostics().find("Raw observation bodies=1"),std::string::npos);
}

namespace {
bool WaitBodyPublication(RuntimeSession& session,uint64_t cursor,ObservationEntry& entry){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
 while(std::chrono::steady_clock::now()<deadline){
  const auto read=session.ReadObservations(cursor,&entry,1);
  if(read.written){cursor=read.next_cursor;if(entry.kind==ObservationKind::Body)return true;}
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 return false;
}
}

TEST(RuntimeSession, SamplingDoesNotIncrementFresh) {
 RuntimeSession session;std::string error;
 ASSERT_TRUE(session.Start(HV_TEST_PROJECT_ROOT,"cpu",1,error))<<error;
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,903,HV_RuntimeClockUs());
 ASSERT_TRUE(session.Submit(frame,error))<<error;
 auto entry=std::make_unique<ObservationEntry>();
 ASSERT_TRUE(WaitBodyPublication(session,0,*entry))<<session.LastError();
 ASSERT_EQ(entry->body_count,1u);EXPECT_EQ(entry->envelope.frame_id,903);
 const auto cursor=entry->sequence;
 for(int i=0;i<30;++i){HV_RuntimeStatsV1 ignored{};session.Copy(HV_RuntimeClockUs(),ignored);}
 EXPECT_EQ(session.StatsV2().fresh_observation_frames,1u);
 EXPECT_EQ(session.ReadObservations(cursor,entry.get(),1).written,0u);
 session.Stop();session.Stop();EXPECT_EQ(session.ErrorCount(),0u);
}

TEST(RuntimeSession, SourceEpochPublishesRestartedIdsWithOriginalEnvelope) {
 RuntimeSession session;std::string error;
 ASSERT_TRUE(session.Start(HV_TEST_PROJECT_ROOT,"cpu",1,error))<<error;
 auto pixels=humanvision::test::ReadBytes(HV_TEST_RAW_IMAGE_PATH);
 FrameEnvelope envelope;envelope.source_id=31;envelope.generation=4;envelope.config_epoch=8;
 envelope.crop={10,20,100,100};envelope.rotation_degrees=90;
 auto frame=humanvision::test::MakeBgrFrame(pixels,218,346,904,HV_RuntimeClockUs());
 ASSERT_TRUE(session.Submit(frame,envelope,error))<<error;
 auto entry=std::make_unique<ObservationEntry>();
 ASSERT_TRUE(WaitBodyPublication(session,0,*entry))<<session.LastError();
 EXPECT_EQ(entry->envelope.source_id,31u);EXPECT_EQ(entry->envelope.generation,4u);
 EXPECT_EQ(entry->envelope.config_epoch,8u);EXPECT_EQ(entry->envelope.crop.x,10);
 EXPECT_EQ(entry->envelope.rotation_degrees,90);EXPECT_EQ(entry->body_count,1u);
 const auto cursor=entry->sequence;
 envelope.generation=5;envelope.config_epoch=9;
 frame.frame_id=1;frame.timestamp_us=HV_RuntimeClockUs();
 ASSERT_TRUE(session.Submit(frame,envelope,error))<<error;
 ASSERT_TRUE(WaitBodyPublication(session,cursor,*entry))<<session.LastError();
 EXPECT_EQ(entry->envelope.frame_id,1);EXPECT_EQ(entry->envelope.generation,5u);
 EXPECT_EQ(entry->envelope.config_epoch,9u);EXPECT_EQ(entry->body_count,1u);
 EXPECT_EQ(session.StatsV2().fresh_observation_frames,2u);
 EXPECT_EQ(session.StatsV2().source_frame_id,1);
 envelope.generation=4;
 EXPECT_FALSE(session.Submit(frame,envelope,error));EXPECT_NE(error.find("expired"),std::string::npos);
}
