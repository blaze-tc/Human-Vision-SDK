#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace humanvision::runtime {
// Optional internal telemetry, deliberately outside the frozen plugin ABI.
// A backend publishes value snapshots; the host never references vendor code.
struct BackendStageDiagnostics {
    char runtime_version[256]{};
    char driver_version[256]{};
    uint32_t core_mask = 0;
    float initialize_ms = 0;
    float input_set_ms = 0;
    float execute_ms = 0;
    float output_get_ms = 0;
    float output_release_ms = 0;
};
namespace detail {
struct BackendDiagnosticSlot { const void* owner = nullptr; BackendStageDiagnostics value{}; };
inline std::mutex backend_stage_mutex;
inline std::array<BackendDiagnosticSlot,32> backend_stage_slots{};
}
inline bool RegisterBackendDiagnostics(const void* owner) {
    if(!owner)return false;
    std::lock_guard<std::mutex> lock(detail::backend_stage_mutex);
    for(auto& slot:detail::backend_stage_slots)if(slot.owner==owner){slot.value={};return true;}
    for(auto& slot:detail::backend_stage_slots)if(!slot.owner){slot.owner=owner;slot.value={};return true;}
    return false;
}
inline void PublishBackendDiagnostics(const void* owner,const BackendStageDiagnostics& value) {
    std::lock_guard<std::mutex> lock(detail::backend_stage_mutex);
    for(auto& slot:detail::backend_stage_slots)if(slot.owner==owner){slot.value=value;return;}
}
inline bool CopyBackendDiagnostics(const void* owner,BackendStageDiagnostics& value) {
    std::lock_guard<std::mutex> lock(detail::backend_stage_mutex);
    for(const auto& slot:detail::backend_stage_slots)if(slot.owner==owner){value=slot.value;return true;}
    return false;
}
inline void UnregisterBackendDiagnostics(const void* owner) {
    std::lock_guard<std::mutex> lock(detail::backend_stage_mutex);
    for(auto& slot:detail::backend_stage_slots)if(slot.owner==owner){slot={};return;}
}
}
