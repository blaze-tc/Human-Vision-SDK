#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_gpu_pipeline.h"
#include "composition/region_assignment.h"
#include "composition/session.h"
#include "services/body_services.h"
#include "gpu/android/unity_vulkan_bridge.h"
#include "host/profile_manager.h"
#include "host/gpu_runtime_host.h"
#include "plugins/backend/ncnn/ncnn_vulkan_backend.h"
#include "plugins/pipeline/simcc/topdown_gpu_pipeline.h"
#include "json/json.hpp"
#include "picosha2/picosha2.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

using namespace humanvision::runtime;
namespace {
nlohmann::json Load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Missing real fixture: " + path.string());
    nlohmann::json value; stream >> value; return value;
}
std::string Hash(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), {});
    return picosha2::hash256_hex_string(bytes);
}
std::vector<float> ReadTensor(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Missing tensor: " + path.string());
    const auto bytes = stream.tellg();
    if (bytes <= 0 || bytes % sizeof(float)) throw std::runtime_error("Invalid tensor bytes");
    std::vector<float> values(static_cast<size_t>(bytes) / sizeof(float));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(values.data()), bytes);
    if (!stream) throw std::runtime_error("Incomplete tensor read");
    return values;
}
// Replace only hardware inference/lease completion with immutable saved real
// GPU tensors. Production pipeline admission, decoding and services stay real.
struct SavedTensorBackend { std::vector<float> detection, points; };
bool Complete(void*, humanvision::gpu::ConsumerFrame& lease, bool, std::string&) noexcept {
    lease.ncnn_role_complete = true; lease.claimed = false; return true;
}
HV_Result HV_CALL Run(void* opaque, const HV_GpuFrameRefV1* frame,
    const HV_GpuImageTransformV1*, HV_TensorViewV1* views, uint32_t capacity,
    uint32_t* count, HV_ErrorBufferV1*) {
    auto& saved = *static_cast<SavedTensorBackend*>(opaque);
    if (capacity < 2) return HV_ERR_INVALID_ARGUMENT;
    for (int i = 0; i < 2; ++i) {
        auto& view = views[i]; view = {};
        view.struct_size = sizeof(view); view.api_version = HV_PLUGIN_API_V1;
        view.name = i ? "out1" : "out0"; view.element_type = 1; view.rank = 2;
        view.dimensions[0] = 5040; view.dimensions[1] = i ? 51 : 65;
        const auto& values = i ? saved.points : saved.detection;
        view.data = values.data(); view.byte_count = values.size() * sizeof(float);
    }
    auto& lease = *static_cast<humanvision::gpu::ConsumerFrame*>(frame->opaque_slot);
    lease.claimed = true; lease.ncnn_role_complete = false;
    lease.role_owner = opaque; lease.complete_role = Complete;
    *count = 2; return HV_OK;
}
const HV_GpuBackendApiV2 backend{{sizeof(HV_GpuBackendApiV2), HV_GPU_FRAME_API_V1,
    nullptr, nullptr, Run, nullptr}, nullptr};
HV_Result HV_CALL Create(void* context, const HV_GpuBackendConfigV1*,
    const HV_GpuDeviceContextV1*, const HV_GpuBackendApiV2** api, void** session,
    HV_ErrorBufferV1*) {
    *api = &backend; *session = context; return HV_OK;
}
void HV_CALL Release(void*, const HV_GpuBackendApiV2*, void*) {}
// A deliberately nonconforming plugin control: actual YOLO config8 emits seven
// real candidates while the host has authorized only four. The host must reject
// this output rather than allowing the Region fix to bypass its count guard.
HV_Result HV_CALL OversizeCreate(const HV_PipelineConfigV1* config,
    const HV_HostServicesV3* host, void** instance, HV_ErrorBufferV1* error) {
    HV_PluginApiV3 plugin{}; plugin.v1.struct_size = sizeof(plugin);
    plugin.v1.api_version = HV_PLUGIN_API_V3;
    if (HV_QueryYoloGpuPipelineV3(HV_PLUGIN_API_V3, &plugin) != HV_OK) return HV_ERR_INTERNAL;
    auto oversized = *config; oversized.max_bodies = 8;
    return plugin.gpu_pipeline->create(&oversized, host, instance, error);
}

struct SavedFrameSource final : GpuConsumerSource {
    std::atomic<bool> ready{false};
    humanvision::gpu::SlotResult Claim(humanvision::gpu::ConsumerFrame& lease) noexcept override {
        using humanvision::gpu::SlotResult;
        if (!ready.exchange(false)) return SlotResult::NoReady;
        lease.claimed = true; lease.ahb_buffer = 42;
        lease.token.generation = lease.metadata.generation = 1;
        lease.token.frame_id = lease.metadata.frame_id = 1500;
        lease.metadata.timestamp_us = 123456;
        lease.metadata.capture_steady_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() - 5000;
        return SlotResult::Ok;
    }
    humanvision::gpu::SlotResult Retire(humanvision::gpu::ConsumerFrame&) noexcept override {
        return humanvision::gpu::SlotResult::Invalid;
    }
    void Quarantine(humanvision::gpu::ConsumerFrame& lease) noexcept override { lease.claimed = false; }
    bool DrainUnsubmitted(humanvision::gpu::ConsumerFrame& lease) noexcept override {
        lease.claimed = false; return true;
    }
    uint64_t Generation() const noexcept override { return 1; }
    uint32_t Width() const noexcept override { return 1024; }
    uint32_t Height() const noexcept override { return 576; }
};

class YoloRegionCandidateFixture : public ::testing::Test {
protected:
    SavedTensorBackend saved;
    nlohmann::json annotations;
    std::string manifest, profile;
    HV_PluginApiV3 plugin{};
    std::shared_ptr<const RuntimeProfile> resolved;
    void SetUp() override {
        const auto project = std::filesystem::path(HV_TEST_PROJECT_ROOT);
        const auto evidence = Load(project / "tools/models/ncnn/yolo_model_gate_evidence.json");
        nlohmann::json record;
        for (const auto& row : evidence.at("fixtures"))
            if (row.at("fixture") == "seven-640") record = row;
        ASSERT_FALSE(record.is_null());
        const auto root = project / "out" / record.at("archive").get<std::string>();
        ASSERT_EQ(Hash(root / "fixture.json"), record.at("fixture_sha256"));
        const auto metadata = Load(root / "fixture.json");
        ASSERT_EQ(metadata.at("sequential_frame_index"), 1500);
        ASSERT_EQ(metadata.at("source_rgba_sha256"),
            "83db08727c2db6aafe2369e71f689428c3e9197176f2d10933a839b141ab9b67");
        ASSERT_EQ(metadata.at("geometry").at("source_width"), 1024);
        ASSERT_EQ(metadata.at("geometry").at("source_height"), 576);
        ASSERT_EQ(metadata.at("geometry").at("width"), 640);
        ASSERT_EQ(metadata.at("geometry").at("height"), 384);
        annotations = metadata.at("annotations"); ASSERT_EQ(annotations.size(), 7u);
        for (const char* name : {"ncnn-gpu-fp32-out0.fp32", "ncnn-gpu-fp32-out1.fp32"})
            ASSERT_EQ(Hash(root / name), record.at("output_sha256").at(name));
        saved.detection = ReadTensor(root / "ncnn-gpu-fp32-out0.fp32");
        saved.points = ReadTensor(root / "ncnn-gpu-fp32-out1.fp32");
        ASSERT_EQ(saved.detection.size(), 5040u * 65); ASSERT_EQ(saved.points.size(), 5040u * 51);
        const auto runtime = project / "out/android-yolo/runtime-rectangle640x384-arm-verified";
        manifest = Load(runtime / "modelpacks/yolov8n-pose-rectangle640x384-fp32-local/modelpack.json").dump();
        profile = Load(runtime / "profiles/android-ncnn-vulkan.json").dump();
        plugin.v1.struct_size = sizeof(plugin); plugin.v1.api_version = HV_PLUGIN_API_V3;
        ASSERT_EQ(HV_QueryYoloGpuPipelineV3(HV_PLUGIN_API_V3, &plugin), HV_OK);
        BackendFactory factory({}, false); PluginRegistry registry; std::string error;
        ASSERT_TRUE(factory.RegisterV3(HV_QueryNcnnVulkanPluginV3, error)) << error;
        ASSERT_TRUE(factory.RegisterV3(HV_QueryYoloGpuPipelineV3, error)) << error;
        resolved = ProfileManager(runtime / "profiles").Resolve("android-ncnn-vulkan", 4,
            registry, ModelPackManager(runtime / "modelpacks"), error, &factory);
        ASSERT_TRUE(resolved) << error;
        ASSERT_EQ(resolved->max_people, 4);
    }
    void Process(int public_capacity, HV_GpuObservationFrameV3& out) {
        HV_HostServicesV3 host{}; host.v2.v1.struct_size = sizeof(host);
        host.v2.v1.api_version = HV_PLUGIN_API_V1; host.v2.v1.context = &saved;
        host.create_gpu_backend_v3 = Create; host.release_gpu_backend_v3 = Release;
        HV_PipelineConfigV1 config{sizeof(config), HV_PLUGIN_API_V1, public_capacity, 0,
            manifest.c_str(), "saved-real-seven-640", profile.c_str()};
        void* instance = nullptr;
        ASSERT_EQ(plugin.gpu_pipeline->create(&config, &host, &instance, nullptr), HV_OK);
        const auto destroy = [this](void* p) { plugin.gpu_pipeline->destroy(p); };
        std::unique_ptr<void, decltype(destroy)> cleanup(instance, destroy);
        humanvision::gpu::ConsumerFrame lease{};
        HV_GpuFrameRefV1 frame{sizeof(frame), HV_GPU_FRAME_API_V1, &lease,
            1024, 576, 1500, 123456, 1, HV_GPU_IMAGE_RGBA8_UNORM, 0};
        out = {}; out.v1.struct_size = sizeof(out); out.v1.api_version = HV_PLUGIN_API_V1;
        char message[256]{}; HV_ErrorBufferV1 error{sizeof(error), HV_PLUGIN_API_V1, message, sizeof(message)};
        ASSERT_EQ(plugin.gpu_pipeline->process_gpu(instance, &frame, &out.v1, &error), HV_OK) << message;
        ASSERT_TRUE(lease.ncnn_role_complete); ASSERT_FALSE(lease.claimed);
    }
    void ProcessComposition(const RuntimeProfile& configuration, HV_GpuObservationFrameV3& out) {
        const auto root = configuration.gpu_pack->root.u8string();
        const auto config = detail::BuildGpuPipelineConfig(configuration, root.c_str());
        HV_HostServicesV3 host{}; host.v2.v1.struct_size = sizeof(host);
        host.v2.v1.api_version = HV_PLUGIN_API_V1; host.v2.v1.context = &saved;
        host.create_gpu_backend_v3 = Create; host.release_gpu_backend_v3 = Release;
        SavedFrameSource source; GpuRuntimeHost worker(source); std::string error;
        ASSERT_TRUE(worker.Start(configuration.gpu_body, host, config, error)) << error;
        source.ready = true;
        bool received = false; int64_t revision = -1;
        for (int i = 0; i < 200 && !received && !worker.PoseJobDrops(); ++i) {
            received = worker.CopyLatest(out, revision);
            if (!received) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_TRUE(received) << worker.LastError();
        EXPECT_EQ(revision, 0); EXPECT_EQ(worker.PoseJobDrops(), 0u);
        EXPECT_EQ(out.v1.source_frame_id, 1500); EXPECT_EQ(out.v1.source_timestamp_us, 123456);
    }
};
class YoloRegionCandidateSelection : public YoloRegionCandidateFixture,
    public ::testing::WithParamInterface<bool> {};

TEST_F(YoloRegionCandidateFixture, NoRegionsKeepsPublicFourAndOriginalLargestBoxSubset) {
    HV_GpuObservationFrameV3 baseline{}, configured{};
    Process(4, baseline); ASSERT_FALSE(HasFatalFailure());
    ProcessComposition(*resolved, configured); ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(configured.v1.body_count, 7u);
    const auto selected = AssignRegions(configured.v1, {}, 0, {}, configured.detector_scores);
    BodyServices services; services.Configure(resolved->max_people, nullptr, 0, 0);
    services.Observe(selected.frame, 0, false, selected.region_indices.data());
    const auto actual = services.Raw(); ASSERT_EQ(actual.count, 4u);
    for (uint32_t i = 0; i < actual.count; ++i) {
        EXPECT_FLOAT_EQ(actual.bodies[i].bbox_px.x, baseline.v1.bodies[i].bbox_px.x);
        EXPECT_FLOAT_EQ(actual.bodies[i].bbox_px.y, baseline.v1.bodies[i].bbox_px.y);
        EXPECT_EQ(actual.bodies[i].source_frame_id, 1500);
    }
}

TEST_F(YoloRegionCandidateFixture, CandidateBudgetHonorsSmallerPackAndPluginLimits) {
    struct Limits { int pack, plugin, candidates; };
    for (const auto limits : {Limits{6, 8, 6}, Limits{8, 6, 6}, Limits{4, 8, 4}, Limits{8, 4, 4}}) {
        SCOPED_TRACE(::testing::Message() << "pack=" << limits.pack << ", plugin=" << limits.plugin);
        RuntimeProfile configuration = *resolved;
        auto pack = std::make_shared<ModelPack>(*resolved->gpu_pack);
        auto module = std::make_shared<GpuPluginModuleV3>(*resolved->gpu_body);
        pack->max_people = limits.pack; module->api.v1.max_people = limits.plugin;
        auto metadata = nlohmann::json::parse(pack->manifest_json);
        metadata["max_people"] = limits.pack; pack->manifest_json = metadata.dump();
        configuration.gpu_pack = pack; configuration.gpu_body = module;
        HV_GpuObservationFrameV3 out{}; ProcessComposition(configuration, out);
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_EQ(out.v1.body_count, uint32_t(limits.candidates));
        BodyServices services; services.Configure(configuration.max_people, nullptr, 0, 0);
        services.Observe(out.v1, 0, false);
        EXPECT_EQ(services.Raw().count, 4u);
    }
}

TEST_F(YoloRegionCandidateFixture, TopDownRetainsPublicRoiCapacity) {
    RuntimeProfile configuration = *resolved;
    auto module = std::make_shared<GpuPluginModuleV3>();
    module->api.v1.struct_size = sizeof(module->api); module->api.v1.api_version = HV_PLUGIN_API_V3;
    ASSERT_EQ(HV_QueryTopDownGpuPipelineV3(HV_PLUGIN_API_V3, &module->api), HV_OK);
    auto pack = std::make_shared<ModelPack>(*resolved->gpu_pack);
    pack->pipeline_id = module->api.v1.plugin_id;
    configuration.gpu_body = module; configuration.gpu_pack = pack;
    const auto root = pack->root.u8string();
    EXPECT_EQ(detail::BuildGpuPipelineConfig(configuration, root.c_str()).max_bodies, 4);
}

TEST_F(YoloRegionCandidateFixture, HostStillRejectsActualYoloOutputAboveConfiguredBudget) {
    auto module = std::make_shared<GpuPluginModuleV3>(*resolved->gpu_body);
    auto callbacks = *module->api.gpu_pipeline; callbacks.create = OversizeCreate;
    module->api.gpu_pipeline = &callbacks;
    HV_HostServicesV3 host{}; host.v2.v1.struct_size = sizeof(host);
    host.v2.v1.api_version = HV_PLUGIN_API_V1; host.v2.v1.context = &saved;
    host.create_gpu_backend_v3 = Create; host.release_gpu_backend_v3 = Release;
    const auto root = resolved->gpu_pack->root.u8string();
    HV_PipelineConfigV1 config{sizeof(config), HV_PLUGIN_API_V1, 4, 0,
        resolved->gpu_pack->manifest_json.c_str(), root.c_str(), resolved->json.c_str()};
    SavedFrameSource source; GpuRuntimeHost worker(source); std::string error;
    ASSERT_TRUE(worker.Start(module, host, config, error)) << error;
    source.ready = true;
    for (int i = 0; i < 200 && !worker.PoseJobDrops(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    HV_GpuObservationFrameV3 rejected{}; int64_t revision;
    EXPECT_FALSE(worker.CopyLatest(rejected, revision));
    worker.Stop();
    EXPECT_EQ(worker.PoseJobDrops(), 1u);
    EXPECT_NE(worker.LastError().find("output count"), std::string::npos);
}

TEST_P(YoloRegionCandidateSelection, FourSelectedRegionsRemainPopulatedAtPublicCapacityFour) {
    // Literal source-space regions chosen from independent source-frame annotation.
    // AllRear selects four rear people; MixedRows selects three rear + center front.
    RegionSet regions{}; regions.count = 4;
    regions.rects[0] = {.15f, 0, .09f, 1};
    regions.rects[1] = {.38f, 0, .08f, 1};
    regions.rects[2] = {.60f, 0, .06f, 1};
    regions.rects[3] = GetParam() ? HV_Rect{.53f, 0, .06f, 1} : HV_Rect{.82f, 0, .10f, 1};
    const char* expected_ids[4]{"left-rear", "middle-left-rear", "middle-right-rear",
        GetParam() ? "center-front" : "right-rear"};
    HV_GpuObservationFrameV3 all{}; Process(8, all); ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(all.v1.body_count, 7u);
    int expected_source[4]{-1, -1, -1, -1};
    bool matched[HV_MAX_PEOPLE]{};
    for (const auto& annotation : annotations) {
        const auto& box = annotation.at("bbox_xyxy");
        const float cx = (box[0].get<float>() + box[2].get<float>()) * .5f;
        const float cy = (box[1].get<float>() + box[3].get<float>()) * .5f;
        int best = -1; float distance = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < all.v1.body_count; ++i) if (!matched[i]) {
            const auto& actual = all.v1.bodies[i].bbox_px;
            const float dx = actual.x + actual.width * .5f - cx, dy = actual.y + actual.height * .5f - cy;
            if (dx * dx + dy * dy < distance) { distance = dx * dx + dy * dy; best = int(i); }
        }
        ASSERT_GE(best, 0); ASSERT_LT(distance, 50.f * 50.f) << annotation.at("id");
        matched[best] = true;
        const auto& body = all.v1.bodies[best];
        const auto& left = body.joints[HV_CANONICAL_HIP_LEFT];
        const auto& right = body.joints[HV_CANONICAL_HIP_RIGHT];
        ASSERT_TRUE(left.valid); ASSERT_TRUE(right.valid);
        const float x = (left.x_px + right.x_px) * .5f / 1024;
        const float y = (left.y_px + right.y_px) * .5f / 576;
        int memberships = 0;
        for (uint32_t r = 0; r < regions.count; ++r) {
            const auto& rect = regions.rects[r];
            const bool inside = x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height;
            if (annotation.at("id") == expected_ids[r]) {
                ASSERT_TRUE(inside) << expected_ids[r]; expected_source[r] = best;
            } else ASSERT_FALSE(inside) << annotation.at("id") << " unexpectedly occupies region " << r;
            memberships += inside ? 1 : 0;
        }
        ASSERT_LE(memberships, 1) << "Fixture region anchors must be separated";
    }
    for (int index : expected_source) ASSERT_GE(index, 0);
    const auto oracle = AssignRegions(all.v1, regions, 1, {}, all.detector_scores);
    ASSERT_EQ(oracle.body_count, 4u) << "Real fixture must contain all four requested regions";
    for (uint32_t i = 0; i < oracle.body_count; ++i) {
        const int region = oracle.region_indices[i]; ASSERT_GE(region, 0); ASSERT_LT(region, 4);
        EXPECT_FLOAT_EQ(oracle.frame.bodies[i].bbox_px.x, all.v1.bodies[expected_source[region]].bbox_px.x);
    }
    BodyServices oracle_services; oracle_services.Configure(4, regions.rects.data(), regions.count, 1);
    oracle_services.Observe(oracle.frame, 1, false, oracle.region_indices.data());
    ASSERT_EQ(oracle_services.Raw().count, 4u) << "Output/service capacity four must suffice";

    // Explicit mechanism control: the pipeline obeys config4. Region starvation
    // remains reproducible here; repairing composition must not bypass that ABI.
    HV_GpuObservationFrameV3 limited{}; Process(4, limited); ASSERT_FALSE(HasFatalFailure());
    ASSERT_EQ(limited.v1.body_count, 4u);
    EXPECT_EQ(AssignRegions(limited.v1, regions, 1, {}, limited.detector_scores).body_count, 1u);

    // Uses the exact production config builder called by RuntimeSession::Start
    // with a real profile resolved at public capacity4, through the real GPU host.
    HV_GpuObservationFrameV3 configured{}; ProcessComposition(*resolved, configured); ASSERT_FALSE(HasFatalFailure());
    const auto selected = AssignRegions(configured.v1, regions, 1, {}, configured.detector_scores);
    BodyServices services; services.Configure(4, regions.rects.data(), regions.count, 1);
    services.Observe(selected.frame, 1, false, selected.region_indices.data());
    const auto actual = services.Raw();
    EXPECT_EQ(actual.count, 4u) << "All four independently selected regions exist in the real tensor; "
        << "configured pipeline admitted " << configured.v1.body_count << " candidates before Region assignment";
    for (int region = 0; region < 4; ++region) {
        int occupants = 0;
        for (uint32_t i = 0; i < actual.count; ++i) if (actual.bodies[i].region_index == region) {
            ++occupants;
            EXPECT_FLOAT_EQ(actual.bodies[i].bbox_px.x, all.v1.bodies[expected_source[region]].bbox_px.x);
            EXPECT_EQ(actual.bodies[i].source_frame_id, 1500);
            EXPECT_EQ(actual.bodies[i].region_revision, 1);
        }
        EXPECT_EQ(occupants, 1) << "Missing " << expected_ids[region] << " in region " << region;
    }
}
INSTANTIATE_TEST_SUITE_P(RealSeven640, YoloRegionCandidateSelection, ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "MixedRows" : "AllRear"; });
}
