#include "humanvision/humanvision_c.h"

#include "core/humanvision_engine.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {

constexpr std::uint64_t kHandleMagic = 0x485653444B443032ULL;

struct HandleState {
    std::uint64_t magic = kHandleMagic;
    std::unique_ptr<humanvision::HumanVisionEngine> engine;
};

thread_local std::string g_last_error;

HandleState* ToState(const HV_Handle handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    auto* state = static_cast<HandleState*>(handle);
    return state->magic == kHandleMagic ? state : nullptr;
}

HV_Result InvalidHandle() {
    g_last_error = "HumanVision handle is null or invalid";
    return HV_ERR_NOT_INITIALIZED;
}

}  // namespace

extern "C" HV_Result HV_CALL HV_GetHandJoints(HV_Handle handle, int64_t sequence, HV_Joint* joints, int32_t capacity) {
    auto* state = ToState(handle);
    return state ? state->engine->GetHands(sequence, joints, capacity) : InvalidHandle();
}

extern "C" HV_Result HV_CALL HV_Create(
    const HV_Config* config,
    HV_Handle* out_handle) {
    if (out_handle == nullptr) {
        g_last_error = "out_handle is null";
        return HV_ERR_INVALID_ARGUMENT;
    }
    *out_handle = nullptr;
    std::string error;
    const HV_Result validation = humanvision::ValidateConfig(config, error);
    if (validation != HV_OK) {
        g_last_error = std::move(error);
        return validation;
    }
    try {
        auto state = std::make_unique<HandleState>();
        state->engine = std::make_unique<humanvision::HumanVisionEngine>(*config);
        if (!state->engine->Initialize(error)) {
            g_last_error = std::move(error);
            return HV_ERR_MODEL_LOAD;
        }
        *out_handle = state.release();
        g_last_error.clear();
        return HV_OK;
    } catch (const std::bad_alloc&) {
        g_last_error = "allocation failed while creating HumanVision engine";
        return HV_ERR_INTERNAL;
    } catch (const std::exception& exception) {
        g_last_error = std::string("failed to create HumanVision engine: ") +
                       exception.what();
        return HV_ERR_INTERNAL;
    } catch (...) {
        g_last_error = "unknown failure while creating HumanVision engine";
        return HV_ERR_INTERNAL;
    }
}

extern "C" HV_Result HV_CALL HV_Reconfigure(
    const HV_Handle handle,
    const HV_Config* config) {
    HandleState* state = ToState(handle);
    if (state == nullptr) {
        return InvalidHandle();
    }
    if (config == nullptr) {
        g_last_error = "config is null";
        return HV_ERR_INVALID_ARGUMENT;
    }
    const HV_Result result = state->engine->Reconfigure(*config);
    if (result != HV_OK) {
        g_last_error = state->engine->LastError();
    }
    return result;
}

extern "C" HV_Result HV_CALL HV_SubmitFrame(
    const HV_Handle handle,
    const HV_VideoFrame* frame) {
    HandleState* state = ToState(handle);
    if (state == nullptr) {
        return InvalidHandle();
    }
    const HV_Result result = state->engine->SubmitFrame(frame);
    if (result != HV_OK) {
        g_last_error = state->engine->LastError();
    }
    return result;
}

extern "C" HV_Result HV_CALL HV_GetLatestResultMeta(
    const HV_Handle handle,
    HV_ResultMeta* out_meta) {
    HandleState* state = ToState(handle);
    return state == nullptr ? InvalidHandle()
                            : state->engine->GetLatestResultMeta(out_meta);
}

extern "C" int32_t HV_CALL HV_GetBodyCount(const HV_Handle handle) {
    HandleState* state = ToState(handle);
    if (state == nullptr) {
        InvalidHandle();
        return 0;
    }
    return state->engine->GetBodyCount();
}

extern "C" HV_Result HV_CALL HV_GetBodies(
    const HV_Handle handle,
    HV_Body* out_bodies,
    const int32_t capacity,
    int32_t* written) {
    HandleState* state = ToState(handle);
    return state == nullptr
               ? InvalidHandle()
               : state->engine->GetBodies(out_bodies, capacity, written);
}

extern "C" HV_Result HV_CALL HV_GetStats(
    const HV_Handle handle,
    HV_Stats* out_stats) {
    HandleState* state = ToState(handle);
    return state == nullptr ? InvalidHandle() : state->engine->GetStats(out_stats);
}

extern "C" const char* HV_CALL HV_GetLastError(const HV_Handle handle) {
    HandleState* state = ToState(handle);
    if (state != nullptr) {
        const std::string engine_error = state->engine->LastError();
        if (!engine_error.empty()) {
            g_last_error = engine_error;
        }
    }
    return g_last_error.c_str();
}

extern "C" HV_Result HV_CALL HV_SetRegions(HV_Handle handle, const HV_Rect* regions, int32_t count, int64_t revision) {
    auto* state = ToState(handle);
    if (!state) return InvalidHandle();
    try { return state->engine->SetRegions(regions, count, revision); }
    catch (...) { g_last_error = "Failed to configure recognition regions"; return HV_ERR_INTERNAL; }
}

extern "C" HV_Result HV_CALL HV_GetRegionAssignments(HV_Handle handle, int64_t sequence,
    int32_t* indices, int32_t capacity, int64_t* revision) {
    auto* state = ToState(handle);
    return state ? state->engine->GetRegionAssignments(sequence, indices, capacity, revision) : InvalidHandle();
}

extern "C" void HV_CALL HV_Destroy(const HV_Handle handle) {
    HandleState* state = ToState(handle);
    if (state == nullptr) {
        return;
    }
    state->magic = 0;
    delete state;
}
