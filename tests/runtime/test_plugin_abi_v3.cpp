#include "humanvision_plugin_v3.h"
#include "host/backend_factory.h"
#include <gtest/gtest.h>
#include <cstddef>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

using humanvision::runtime::BackendFactory;

static_assert(sizeof(HV_GpuBackendApiV1) == 40);
static_assert(sizeof(HV_HostServicesV2) == 48);
static_assert(sizeof(HV_GpuPipelineApiV1) == 32);
static_assert(sizeof(HV_PluginApiV2) == 88);
static_assert(offsetof(HV_GpuBackendApiV2, v1) == 0);
static_assert(offsetof(HV_HostServicesV3, v2) == 0);
static_assert(offsetof(HV_PluginApiV3, v1) == 0);

namespace {
struct Fixture { uint64_t generation = 9; uint64_t active_token = 0; bool live = false; int runs = 0; };
Fixture state;
int creates = 0, destroys = 0, pipeline_creates = 0, discards = 0, prepare_invocations = 0;
bool fail_create = false, fail_run = false, fail_discard = false;
bool fail_prepare = false, invalid_prepare_ref = false;
std::atomic<bool> block_image{false}, image_entered{false}, release_image{false};
std::atomic<bool> prepare_attempt_started{false}, info_attempt_started{false};
std::atomic<int> concurrent_prepare_entries{0}, concurrent_info_entries{0};
uint64_t token_sequence[3]{7, 7, 7};
int prepare_calls = 0;

HV_Result HV_CALL Create(const HV_GpuBackendConfigV1*, const HV_GpuDeviceContextV1*, void** out, HV_ErrorBufferV1*) {
    ++creates;
    *out = new Fixture();
    return fail_create ? HV_ERR_MODEL_LOAD : HV_OK;
}
void HV_CALL Destroy(void* p) { ++destroys; delete static_cast<Fixture*>(p); }
HV_Result HV_CALL RunImage(void*, const HV_GpuFrameRefV1*, const HV_GpuImageTransformV1*, HV_TensorViewV1*, uint32_t, uint32_t*, HV_ErrorBufferV1*) {
    if (block_image.load()) {
        image_entered.store(true);
        while (!release_image.load()) std::this_thread::yield();
    }
    return HV_OK;
}
HV_Result HV_CALL Info(void*, HV_BackendSessionInfoV1*) { ++concurrent_info_entries; return HV_OK; }
HV_Result HV_CALL Prepare(void* p, const HV_GpuFrameRefV1* frame, const HV_GpuImageTransformV1*, HV_GpuPreparedRefV1* out, HV_ErrorBufferV1*) {
    ++concurrent_prepare_entries;
    ++prepare_invocations;
    auto* f = static_cast<Fixture*>(p);
    if (f->live) return HV_ERR_INVALID_ARGUMENT;
    f->live = true;
    f->active_token = token_sequence[prepare_calls++ % 3];
    *out = {sizeof(*out), HV_GPU_PREPARED_API_V1, f->active_token, f->generation, frame->frame_id, frame->timestamp_us, 0, 0};
    if (invalid_prepare_ref) out->token = 0;
    return fail_prepare ? HV_ERR_INTERNAL : HV_OK;
}
HV_Result HV_CALL RunPrepared(void* p, const HV_GpuPreparedRefV1* ref, HV_TensorViewV1*, uint32_t, uint32_t* count, HV_ErrorBufferV1*) {
    auto* f = static_cast<Fixture*>(p);
    if (!f->live || ref->generation != f->generation || ref->token != f->active_token) return HV_ERR_INVALID_ARGUMENT;
    if (fail_run) return HV_ERR_INTERNAL;
    f->live = false; ++f->runs; *count = 0; return HV_OK;
}
HV_Result HV_CALL Discard(void* p, const HV_GpuPreparedRefV1* ref, HV_ErrorBufferV1*) {
    ++discards;
    auto* f = static_cast<Fixture*>(p);
    if (!f->live || ref->generation != f->generation || ref->token != f->active_token) return HV_ERR_INVALID_ARGUMENT;
    if (fail_discard) return HV_ERR_INTERNAL;
    f->live = false; return HV_OK;
}
HV_Result HV_CALL PipelineCreate(const HV_PipelineConfigV1*, const HV_HostServicesV3* services, void** out, HV_ErrorBufferV1*) {
    if (!services || !services->create_gpu_backend_v3 || services->v2.v1.api_version != 1) return HV_ERR_INVALID_ARGUMENT;
    ++pipeline_creates; *out = new int(1); return HV_OK;
}
void HV_CALL PipelineDestroy(void* p) { delete static_cast<int*>(p); }
HV_Result HV_CALL Process(void*, const HV_GpuFrameRefV1*, HV_ObservationFrameV1*, HV_ErrorBufferV1*) { return HV_OK; }
HV_GpuPreparedApiV1 prepared{sizeof(prepared), HV_GPU_PREPARED_API_V1, Prepare, RunPrepared, Discard};
HV_GpuBackendApiV2 backend{{sizeof(HV_GpuBackendApiV2), HV_GPU_FRAME_API_V1, Create, Destroy, RunImage, Info}, &prepared};
HV_GpuPipelineApiV2 pipeline{sizeof(pipeline), HV_GPU_PIPELINE_API_V2, PipelineCreate, PipelineDestroy, Process};
HV_PluginApiV3 offered{};
void Reset(bool has_backend = true, bool has_pipeline = false) {
    state = {}; creates = destroys = pipeline_creates = prepare_calls = prepare_invocations = discards = 0;
    fail_create = fail_run = fail_discard = fail_prepare = invalid_prepare_ref = false;
    block_image.store(false); image_entered.store(false); release_image.store(false);
    prepare_attempt_started.store(false); info_attempt_started.store(false);
    concurrent_prepare_entries.store(0); concurrent_info_entries.store(0);
    token_sequence[0] = token_sequence[1] = token_sequence[2] = 7;
    offered = {{sizeof(offered), HV_PLUGIN_API_V3, "fixture.v3", "1", has_backend ? HV_PLUGIN_BACKEND : HV_PLUGIN_PIPELINE,
        HV_CAP_GPU_INPUT | (has_backend ? HV_CAP_TENSOR_INFERENCE : 0) | (has_pipeline ? HV_CAP_BODY_POSE : 0),
        has_pipeline ? 8u : 0u, nullptr, nullptr, 0}, has_backend ? &backend : nullptr, has_pipeline ? &pipeline : nullptr};
}
HV_Result HV_CALL Query(uint32_t version, HV_PluginApiV3* out) {
    if (version != HV_PLUGIN_API_V3 || !out || out->v1.struct_size != sizeof(*out) || out->v1.api_version != version) return HV_ERR_INVALID_ARGUMENT;
    *out = offered; return HV_OK;
}
HV_Result HV_CALL ThrowQuery(uint32_t, HV_PluginApiV3*) { throw std::runtime_error("query"); }
HV_GpuBackendConfigV1 config{sizeof(config), HV_GPU_FRAME_API_V1, "manifest", "assets", "fixture.v3"};
HV_GpuDeviceContextV1 device{sizeof(device), HV_GPU_FRAME_API_V1, &state};
HV_GpuFrameRefV1 frame{sizeof(frame), HV_GPU_FRAME_API_V1, &state, 320, 320, 88, 1000, 9, HV_GPU_IMAGE_RGBA8_UNORM, 0};
HV_GpuImageTransformV1 transform{sizeof(transform), HV_GPU_FRAME_API_V1};
}

TEST(GpuAbiV3, LayoutAndSeparateQuery) {
    EXPECT_EQ(sizeof(HV_GpuPreparedRefV1), 48u);
    EXPECT_EQ(offsetof(HV_GpuPreparedRefV1, token), 8u);
    EXPECT_EQ(offsetof(HV_GpuPreparedRefV1, generation), 16u);
    EXPECT_EQ(offsetof(HV_GpuPreparedRefV1, flags), 40u);
    EXPECT_EQ(sizeof(HV_GpuPreparedApiV1), 32u);
    EXPECT_EQ(sizeof(HV_GpuBackendApiV2), 48u);
    EXPECT_EQ(sizeof(HV_HostServicesV3), 64u);
    EXPECT_EQ(sizeof(HV_GpuPipelineApiV2), 32u);
    EXPECT_EQ(sizeof(HV_PluginApiV3), 88u);
    static_assert(std::is_same_v<decltype(&HV_QueryPluginV3), HV_QueryPluginV3Fn>);
}

TEST(GpuAbiV3, RejectsMalformedRegistration) {
    for (int fault = 0; fault < 17; ++fault) {
        Reset(true, true); BackendFactory factory({}); std::string error;
        HV_GpuBackendApiV2 b = backend; HV_GpuPreparedApiV1 p = prepared; HV_GpuPipelineApiV2 pipe = pipeline;
        b.prepared = &p; offered.gpu_backend = &b; offered.gpu_pipeline = &pipe;
        switch (fault) {
        case 0: offered.v1.struct_size = sizeof(HV_PluginApiV1); break;
        case 1: offered.v1.api_version = HV_PLUGIN_API_V2; break;
        case 2: offered.v1.plugin_id = ""; break;
        case 3: b.v1.struct_size = 8; break;
        case 4: b.v1.api_version = 2; break;
        case 5: b.v1.run_image = nullptr; break;
        case 6: b.prepared = nullptr; break;
        case 7: p.struct_size = 8; break;
        case 8: p.api_version = 3; break;
        case 9: p.prepare_image = nullptr; break;
        case 10: p.run_prepared = nullptr; break;
        case 11: p.discard_prepared = nullptr; break;
        case 12: pipe.struct_size = 8; break;
        case 13: pipe.api_version = 1; break;
        case 14: pipe.process_gpu = nullptr; break;
        case 15: offered.v1.capabilities = 0; break;
        case 16: b.v1.struct_size = sizeof(HV_GpuBackendApiV1); break;
        }
        EXPECT_FALSE(factory.RegisterV3(Query, error)) << fault;
        EXPECT_FALSE(error.empty()) << fault;
    }
    Reset(); BackendFactory factory({}); std::string error;
    EXPECT_FALSE(factory.RegisterV3(nullptr, error));
    EXPECT_FALSE(factory.RegisterV3(ThrowQuery, error));
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    EXPECT_FALSE(factory.RegisterV3(Query, error));
}

TEST(GpuAbiV3, ExplicitProviderAndOneShotGenerationBoundToken) {
    Reset(); BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    auto services = factory.ServicesV3();
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    config.requested_provider_utf8 = "backend.ort.cpu";
    EXPECT_EQ(services.create_gpu_backend_v3(services.v2.v1.context, &config, &device, &api, &instance, nullptr), HV_ERR_MODEL_LOAD);
    EXPECT_EQ(creates, 0);
    config.requested_provider_utf8 = "fixture.v3";
    ASSERT_EQ(services.create_gpu_backend_v3(services.v2.v1.context, &config, &device, &api, &instance, nullptr), HV_OK);
    EXPECT_EQ(api->v1.struct_size, sizeof(HV_GpuBackendApiV2));
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    ASSERT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_OK);
    EXPECT_EQ(ref.frame_id, 88);
    HV_GpuPreparedRefV1 blocked{sizeof(blocked), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &blocked, nullptr), HV_ERR_INVALID_ARGUMENT);
    HV_GpuPreparedRefV1 stale = ref; stale.generation = 10;
    uint32_t count = 99;
    EXPECT_EQ(api->prepared->run_prepared(instance, &stale, nullptr, 0, &count, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(count, 0u);
    ASSERT_EQ(api->prepared->run_prepared(instance, &ref, nullptr, 0, &count, nullptr), HV_OK);
    EXPECT_EQ(api->prepared->run_prepared(instance, &ref, nullptr, 0, &count, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(api->prepared->discard_prepared(instance, &ref, nullptr), HV_ERR_INVALID_ARGUMENT);
    HV_GpuPreparedRefV1 repeated{sizeof(repeated), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &repeated, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(repeated.token, 0u);
    services.release_gpu_backend_v3(nullptr, api, instance);
    EXPECT_EQ(destroys, 1);
}

TEST(GpuAbiV3, RejectsMalformedRefsAndCleansPartialCreation) {
    Reset(); BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    fail_create = true;
    EXPECT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_ERR_MODEL_LOAD);
    EXPECT_EQ(destroys, 1);
    fail_create = false;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_OK);
    auto bad = ref; bad.flags = 1;
    EXPECT_EQ(api->prepared->discard_prepared(instance, &bad, nullptr), HV_ERR_INVALID_ARGUMENT);
    bad = ref; bad.reserved = 1;
    EXPECT_EQ(api->prepared->discard_prepared(instance, &bad, nullptr), HV_ERR_INVALID_ARGUMENT);
    bad = ref; bad.token = 0;
    EXPECT_EQ(api->prepared->discard_prepared(instance, &bad, nullptr), HV_ERR_INVALID_ARGUMENT);
    bad = ref; bad.struct_size = 8;
    EXPECT_EQ(api->prepared->discard_prepared(instance, &bad, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(api->prepared->discard_prepared(instance, &ref, nullptr), HV_OK);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, RejectsNonAdjacentDuplicateWithinOneGeneration) {
    Reset(); token_sequence[0] = 7; token_sequence[1] = 8; token_sequence[2] = 7;
    BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    for (int i = 0; i < 2; ++i) {
        HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
        ASSERT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_OK);
        ASSERT_EQ(api->prepared->discard_prepared(instance, &ref, nullptr), HV_OK);
    }
    HV_GpuPreparedRefV1 repeated{sizeof(repeated), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &repeated, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(repeated.token, 0u);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, RetainsFailedDiscardForExplicitRetryOrLeaseCleanup) {
    Reset(); token_sequence[1] = 8;
    BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    ASSERT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_OK);
    fail_run = fail_discard = true;
    uint32_t count = 99;
    EXPECT_EQ(api->prepared->run_prepared(instance, &ref, nullptr, 0, &count, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(count, 0u);
    HV_GpuPreparedRefV1 blocked{sizeof(blocked), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &blocked, nullptr), HV_ERR_INVALID_ARGUMENT);
    fail_discard = false;
    EXPECT_EQ(api->prepared->discard_prepared(instance, &ref, nullptr), HV_OK);
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &blocked, nullptr), HV_OK);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, FailedPrepareDiscardPoisonPreventsSlotReuseAndRetainsTokenForDestroy) {
    Reset(); fail_prepare = fail_discard = true;
    BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(ref.token, 0u);
    EXPECT_EQ(discards, 1);
    fail_prepare = fail_discard = false;
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(prepare_calls, 1);
    EXPECT_EQ(prepare_invocations, 1);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
    EXPECT_EQ(discards, 2);
    EXPECT_EQ(destroys, 1);
}

TEST(GpuAbiV3, InvalidPreparedRefPoisonsLeaseUntilDestroy) {
    Reset(); invalid_prepare_ref = true;
    BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INTERNAL);
    invalid_prepare_ref = false;
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(prepare_calls, 1);
    EXPECT_EQ(prepare_invocations, 1);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
    EXPECT_EQ(destroys, 1);
}

TEST(GpuAbiV3, RejectsShortPreparedOutputWithoutTouchingCanaryOrCallingPlugin) {
    Reset(); BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    struct alignas(HV_GpuPreparedRefV1) ShortOutput {
        uint32_t size, version;
        uint64_t token;
        std::array<unsigned char, sizeof(HV_GpuPreparedRefV1) - 16> canary;
    } short_output{16, HV_GPU_PREPARED_API_V1, 0x8877665544332211ull, {}};
    short_output.canary.fill(0xa5);
    auto* out = reinterpret_cast<HV_GpuPreparedRefV1*>(&short_output);
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, out, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(short_output.token, 0x8877665544332211ull);
    for (auto byte : short_output.canary) EXPECT_EQ(byte, 0xa5);
    EXPECT_EQ(prepare_invocations, 0);
    HV_GpuPreparedRefV1 wrong_version{sizeof(HV_GpuPreparedRefV1), 99, 0x11223344};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &wrong_version, nullptr), HV_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(wrong_version.token, 0x11223344u);
    EXPECT_EQ(prepare_invocations, 0);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, FailedPrepareConsumesTokenEvenAfterSuccessfulDiscard) {
    Reset(); fail_prepare = true;
    BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(discards, 1);
    fail_prepare = false;
    EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_ERR_INTERNAL);
    EXPECT_EQ(prepare_calls, 2);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, SerializesImageInfoAndPrepareCallbacksOnOneInstance) {
    Reset(); BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    const HV_GpuBackendApiV2* api = nullptr; void* instance = nullptr;
    ASSERT_EQ(factory.CreateGpuBackendV3(&config, &device, &api, &instance, nullptr), HV_OK);
    block_image.store(true);
    std::thread image_thread([&] {
        uint32_t count = 0;
        EXPECT_EQ(api->v1.run_image(instance, &frame, &transform, nullptr, 0, &count, nullptr), HV_OK);
    });
    for (int i = 0; i < 1000 && !image_entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool image_was_entered = image_entered.load();
    HV_GpuPreparedRefV1 ref{sizeof(ref), HV_GPU_PREPARED_API_V1};
    HV_BackendSessionInfoV1 info{sizeof(info), HV_PLUGIN_API_V1};
    std::thread prepare_thread([&] {
        prepare_attempt_started.store(true);
        EXPECT_EQ(api->prepared->prepare_image(instance, &frame, &transform, &ref, nullptr), HV_OK);
    });
    std::thread info_thread([&] {
        info_attempt_started.store(true);
        EXPECT_EQ(api->v1.session_info(instance, &info), HV_OK);
    });
    for (int i = 0; i < 1000 && (!prepare_attempt_started.load() || !info_attempt_started.load()); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool both_attempted = prepare_attempt_started.load() && info_attempt_started.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const int prepare_while_image = concurrent_prepare_entries.load();
    const int info_while_image = concurrent_info_entries.load();
    release_image.store(true);
    image_thread.join(); prepare_thread.join(); info_thread.join();
    EXPECT_TRUE(image_was_entered);
    EXPECT_TRUE(both_attempted);
    EXPECT_EQ(prepare_while_image, 0);
    EXPECT_EQ(info_while_image, 0);
    EXPECT_EQ(concurrent_prepare_entries.load(), 1);
    EXPECT_EQ(concurrent_info_entries.load(), 1);
    if (ref.token) api->prepared->discard_prepared(instance, &ref, nullptr);
    factory.ServicesV3().release_gpu_backend_v3(nullptr, api, instance);
}

TEST(GpuAbiV3, PassesV3ServicesOnlyToV3Pipeline) {
    Reset(false, true); BackendFactory factory({}); std::string error;
    ASSERT_TRUE(factory.RegisterV3(Query, error)) << error;
    auto module = factory.FindV3("fixture.v3", HV_CAP_GPU_INPUT, error);
    ASSERT_NE(module, nullptr);
    auto services = factory.ServicesV3();
    HV_PipelineConfigV1 cfg{sizeof(cfg), HV_PLUGIN_API_V1, 1, 0, "manifest", "assets", nullptr};
    void* instance = nullptr;
    ASSERT_EQ(module->api.gpu_pipeline->create(&cfg, &services, &instance, nullptr), HV_OK);
    EXPECT_EQ(pipeline_creates, 1);
    module->api.gpu_pipeline->destroy(instance);
}
