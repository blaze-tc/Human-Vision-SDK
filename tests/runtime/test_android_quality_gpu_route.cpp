#include "composition/session.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "json/json.hpp"
#include "picosha2/picosha2.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <fstream>

using namespace humanvision::runtime;
namespace {
// Configuration/creation fixture only: no model inference or synthetic joints.
std::atomic<int> creates{0};
HV_Result HV_CALL Create(const HV_PipelineConfigV1*, const HV_HostServicesV3*, void** out, HV_ErrorBufferV1*) {
    ++creates; *out = &creates; return HV_OK;
}
void HV_CALL Destroy(void*) {}
HV_Result HV_CALL Process(void*, const HV_GpuFrameRefV1*, HV_ObservationFrameV1*, HV_ErrorBufferV1*) {
    return HV_ERR_INTERNAL; // Never invoked: the source has no frames.
}
const HV_GpuPipelineApiV2 pipeline{sizeof(pipeline), HV_GPU_PIPELINE_API_V2, Create, Destroy, Process};
HV_Result HV_CALL Query(uint32_t version, HV_PluginApiV3* out) {
    if (version != HV_PLUGIN_API_V3 || !out || out->v1.struct_size < sizeof(*out)) return HV_ERR_INVALID_ARGUMENT;
    *out = {}; out->v1 = {sizeof(*out), HV_PLUGIN_API_V3, "fixture.quality", "1", HV_PLUGIN_PIPELINE,
        HV_CAP_BODY_POSE | HV_CAP_MULTI_PERSON | HV_CAP_GPU_INPUT, 8, nullptr, nullptr, 0};
    out->gpu_pipeline = &pipeline; return HV_OK;
}
struct EmptySource final : GpuConsumerSource {
    humanvision::gpu::SlotResult Claim(humanvision::gpu::ConsumerFrame&) noexcept override { return humanvision::gpu::SlotResult::NoReady; }
    humanvision::gpu::SlotResult Retire(humanvision::gpu::ConsumerFrame&) noexcept override { return humanvision::gpu::SlotResult::Invalid; }
    void Quarantine(humanvision::gpu::ConsumerFrame&) noexcept override {}
    bool DrainUnsubmitted(humanvision::gpu::ConsumerFrame&) noexcept override { return false; }
    uint64_t Generation() const noexcept override { return 1; }
    uint32_t Width() const noexcept override { return 320; }
    uint32_t Height() const noexcept override { return 240; }
};
class AndroidQualityGpuRoute : public testing::TestWithParam<const char*> {
protected:
    std::filesystem::path root;
    nlohmann::json profile, pack;
    void SetUp() override {
        root = std::filesystem::temp_directory_path() / ("hv-quality-route-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "profiles");
        const auto folder = root / "modelpacks" / "fixture";
        std::filesystem::create_directories(folder);
        for (const char* file : {"detector.param", "detector.bin"}) std::ofstream(folder / file, std::ios::binary) << "abc";
        profile = {{"schema_version", 1}, {"profile", GetParam()},
            {"body", {{"pipeline", "fixture.quality"}, {"modelPack", "fixture"}}},
            {"hands", {{"enabled", false}}},
            {"detector", {{"cadence_interval_frames", 4}, {"max_capture_gap_us", 200000}}},
            {"backend", {{"preference", {"backend.ncnn.vulkan"}}, {"allow_fallback", false}}},
            {"required_capabilities", {"body_pose", "multi_person", "gpu_input", "vulkan", "android-hardware-buffer", "external-sync-fd"}}};
        // Reuse the established schema-2 local FP32 contract; GPU route selection
        // depends on capabilities and registration, not the particular decoder.
        pack = nlohmann::json::parse(R"({"schema_version":2,"pack_id":"fixture","pack_version":"2.0.0","pipeline_id":"fixture.quality","max_people":8,"local_evaluation_only":true,"execution_contract":"raw_tensor_fp32_v1","capabilities":["body_pose","multi_person","gpu_input","vulkan","android-hardware-buffer","external-sync-fd"],"models":[{"role":"detector","format":"ncnn","decoder_id":"rtmdet_nano_raw_v1","param_path":"detector.param","bin_path":"detector.bin","param_sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","bin_sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","input_contract":{"image_format":"rgba8-unorm","color_order":"rgb","normalization":{"mean":[0,0,0],"norm":[1,1,1]},"tensor_dtype":"fp32","elempack":1,"width":320,"height":320,"input_blob":"in0"},"output_contract":{"decoder":"rtmdet_nano_raw_v1","output_blobs":["cls","bbox"]},"source":"configuration fixture","license":"test-only","conversion_recipe":"configuration fixture"}]})");
        Save();
    }
    void TearDown() override { std::filesystem::remove_all(root); }
    void Save() {
        const auto bytes = profile.dump();
        std::ofstream(root / "profiles" / (profile.at("profile").get<std::string>() + ".json"), std::ios::binary) << bytes;
        pack["profile_sha256"] = picosha2::hash256_hex_string(bytes);
        SavePack();
    }
    void SavePack() { std::ofstream(root / "modelpacks/fixture/modelpack.json", std::ios::binary) << pack.dump(); }
};

TEST_P(AndroidQualityGpuRoute, ResolvesV3GpuWithoutCpuOrFallbackAtEveryCapacity) {
    PluginRegistry cpu; ModelPackManager packs(root / "modelpacks"); BackendFactory gpu({}, false); std::string error;
    ASSERT_TRUE(gpu.RegisterV3(HV_QueryNcnnVulkanPluginV3, error)) << error;
    ASSERT_TRUE(gpu.RegisterV3(Query, error)) << error;
    for (int capacity = 1; capacity <= 8; ++capacity) {
        auto selected = ProfileManager(root / "profiles").Resolve(GetParam(), capacity, cpu, packs, error, &gpu);
        ASSERT_TRUE(selected) << error;
        EXPECT_TRUE(selected->gpu_route); EXPECT_TRUE(selected->gpu_body); EXPECT_TRUE(selected->gpu_pack);
        EXPECT_FALSE(selected->body.plugin); EXPECT_TRUE(selected->backends.empty());
        EXPECT_FALSE(selected->allow_backend_fallback); EXPECT_EQ(selected->max_people, capacity);
    }
}
TEST_P(AndroidQualityGpuRoute, RuntimeSessionStartsV3PipelineRatherThanCpu) {
    EmptySource source; creates = 0; std::string error;
    RuntimeSession session;
    ASSERT_TRUE(session.Start(root, GetParam(), 8, error, Query, &source)) << error;
    EXPECT_EQ(creates.load(), 1);
    HV_VideoFrame cpu{};
    EXPECT_FALSE(session.Submit(cpu, error));
}
TEST_P(AndroidQualityGpuRoute, KeepsGpuRegistrationHashBackendAndCapabilityGuards) {
    PluginRegistry cpu; ModelPackManager packs(root / "modelpacks"); BackendFactory gpu({}, false); std::string error;
    auto resolve = [&] { return ProfileManager(root / "profiles").Resolve(GetParam(), 8, cpu, packs, error, &gpu); };
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("V3 GPU pipeline unavailable"), std::string::npos) << error;
    ASSERT_TRUE(gpu.RegisterV3(HV_QueryNcnnVulkanPluginV3, error)); ASSERT_TRUE(gpu.RegisterV3(Query, error));
    pack["profile_sha256"] = std::string(64, '0'); SavePack();
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("profile SHA-256"), std::string::npos) << error;
    Save();
    profile["backend"]["allow_fallback"] = true; Save();
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("allow_fallback false"), std::string::npos) << error;
    profile["backend"]["allow_fallback"] = false;
    profile["backend"]["preference"] = {"backend.ort.cpu"}; Save();
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("exactly backend.ncnn.vulkan"), std::string::npos) << error;
    profile["backend"]["preference"] = {"backend.ncnn.vulkan"};
    profile["required_capabilities"] = nlohmann::json::array(); Save();
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("required capabilities"), std::string::npos) << error;
    profile["required_capabilities"] = {"body_pose", "hand_pose"}; Save();
    EXPECT_FALSE(resolve()); EXPECT_NE(error.find("Missing required GPU capability"), std::string::npos) << error;
}
TEST_P(AndroidQualityGpuRoute, DoesNotAdmitLookalikeOrOrtProfilesToGpu) {
    EmptySource source; std::string error;
    for (const char* id : {"android-ncnn-vulkan-quality-high-extra", "android-ncnn-vulkan-quality-unknown", "android-ort-cpu", "android-ort-xnnpack"}) {
        profile["profile"] = id; Save(); creates = 0;
        RuntimeSession session;
        EXPECT_FALSE(session.Start(root, id, 8, error, Query, &source)) << id;
        EXPECT_EQ(creates.load(), 0) << id;
    }
}
INSTANTIATE_TEST_SUITE_P(ExactProfiles, AndroidQualityGpuRoute, testing::Values(
    "android-ncnn-vulkan", "android-ncnn-vulkan-quality-low", "android-ncnn-vulkan-quality-high"));
}
