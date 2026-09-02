#pragma once

#include "core/latest_frame_slot.h"
#include "core/result_snapshot_store.h"
#include "core/stats_collector.h"
#include "humanvision/humanvision_types.h"
#include "models/rtmdet/rtmdet_model.h"
#include "models/rtmpose/rtmpose_model.h"
#include "tracking/i_body_tracker.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace humanvision {

HV_Result ValidateConfig(const HV_Config* config, std::string& error);

class HumanVisionEngine {
public:
    explicit HumanVisionEngine(const HV_Config& config);
    ~HumanVisionEngine();
    HumanVisionEngine(const HumanVisionEngine&) = delete;
    HumanVisionEngine& operator=(const HumanVisionEngine&) = delete;

    bool Initialize(std::string& error);
    HV_Result Reconfigure(const HV_Config& config);
    HV_Result SubmitFrame(const HV_VideoFrame* frame);
    HV_Result GetLatestResultMeta(HV_ResultMeta* destination) const;
    int GetBodyCount() const;
    HV_Result GetBodies(HV_Body* destination, int capacity, int* written) const;
    HV_Result GetStats(HV_Stats* destination) const;
    std::string LastError() const;

private:
    struct RuntimeConfig {
        int max_bodies = 4;
        float detection_threshold = 0.35F;
        float pose_threshold = 0.30F;
        int detection_interval = 1;
        bool enable_tracking = false;
        HV_Backend backend = HV_BACKEND_ONNX_CPU;
        std::string detector_model_path;
        std::string pose_model_path;
    };

    static RuntimeConfig CopyConfig(const HV_Config& config);
    void WorkerLoop();
    void SetLastError(std::string error);

    mutable std::mutex config_mutex_;
    RuntimeConfig config_;
    LatestFrameSlot frame_slot_;
    ResultSnapshotStore result_store_;
    std::unique_ptr<RtmdetModel> detector_;
    std::unique_ptr<RtmposeModel> pose_;
    std::unique_ptr<IBodyTracker> tracker_;
    std::thread worker_;

    StatsCollector stats_;

    mutable std::mutex error_mutex_;
    std::string last_error_;
    std::int64_t result_sequence_ = 0;
    std::int64_t processed_input_frames_ = 0;
};

}  // namespace humanvision
