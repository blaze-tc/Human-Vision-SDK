#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace humanvision::runtime {

// Internal process telemetry. This is deliberately outside the versioned plugin
// ABI; built-in static pipelines publish into a fixed allocation-free registry.
struct PipelineDiagnostics {
    uint32_t raw_detection_count = 0;
    uint32_t accepted_detection_count = 0;
    float max_detection_score = 0;
    float detector_inference_ms = 0;
    uint64_t detector_execution_count = 0;
    float detector_fps = 0;
    float pose_inference_total_ms = 0;
    uint32_t pose_person_count = 0;
};

namespace detail {
struct PipelineDiagnosticSlot {
    const void* owner = nullptr;
    PipelineDiagnostics value{};
};
inline std::mutex pipeline_diagnostic_mutex;
inline std::array<PipelineDiagnosticSlot,16> pipeline_diagnostic_slots{};
}

inline bool RegisterPipelineDiagnostics(const void* owner) {
    if(!owner)return false;
    std::lock_guard<std::mutex> lock(detail::pipeline_diagnostic_mutex);
    for(auto& slot:detail::pipeline_diagnostic_slots)if(slot.owner==owner){slot.value={};return true;}
    for(auto& slot:detail::pipeline_diagnostic_slots)if(!slot.owner){slot.owner=owner;slot.value={};return true;}
    return false;
}

inline void PublishPipelineDiagnostics(const void* owner,const PipelineDiagnostics& value) {
    std::lock_guard<std::mutex> lock(detail::pipeline_diagnostic_mutex);
    for(auto& slot:detail::pipeline_diagnostic_slots)if(slot.owner==owner){slot.value=value;return;}
}

inline bool CopyPipelineDiagnostics(const void* owner,PipelineDiagnostics& value) {
    std::lock_guard<std::mutex> lock(detail::pipeline_diagnostic_mutex);
    for(const auto& slot:detail::pipeline_diagnostic_slots)if(slot.owner==owner){value=slot.value;return true;}
    return false;
}

inline void UnregisterPipelineDiagnostics(const void* owner) {
    std::lock_guard<std::mutex> lock(detail::pipeline_diagnostic_mutex);
    for(auto& slot:detail::pipeline_diagnostic_slots)if(slot.owner==owner){slot={};return;}
}

}
