#include "core/humanvision_engine.h"

#include "backend/onnx/onnx_runtime_backend.h"
#include "tracking/center_iou_tracker.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace humanvision {

HV_Result ValidateConfig(const HV_Config* config, std::string& error) {
    if (config == nullptr) {
        error = "config is null";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->struct_size < static_cast<std::int32_t>(sizeof(HV_Config))) {
        error = "config.struct_size is smaller than HV_Config";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->max_bodies < 1) {
        error = "config.max_bodies must be at least 1";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->detection_threshold < 0.0F || config->detection_threshold > 1.0F ||
        config->pose_threshold < 0.0F || config->pose_threshold > 1.0F) {
        error = "detection_threshold and pose_threshold must be in [0, 1]";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->detection_interval < 1) {
        error = "config.detection_interval must be at least 1";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->enable_tracking != 0 && config->enable_tracking != 1) {
        error = "config.enable_tracking must be 0 or 1";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->backend != HV_BACKEND_AUTO &&
        config->backend != HV_BACKEND_ONNX_CPU) {
        error = "requested backend is not implemented in D0";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->detector_model_path_utf8 == nullptr ||
        config->detector_model_path_utf8[0] == '\0') {
        error = "config.detector_model_path_utf8 is required";
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (config->pose_model_path_utf8 == nullptr ||
        config->pose_model_path_utf8[0] == '\0') {
        error = "config.pose_model_path_utf8 is required";
        return HV_ERR_INVALID_ARGUMENT;
    }
    error.clear();
    return HV_OK;
}

HumanVisionEngine::RuntimeConfig HumanVisionEngine::CopyConfig(
    const HV_Config& config) {
    RuntimeConfig copy;
    copy.max_bodies = config.max_bodies;
    copy.detection_threshold = config.detection_threshold;
    copy.pose_threshold = config.pose_threshold;
    copy.detection_interval = config.detection_interval;
    copy.enable_tracking = config.enable_tracking != 0;
    copy.backend = config.backend == HV_BACKEND_AUTO ? HV_BACKEND_ONNX_CPU
                                                     : config.backend;
    copy.detector_model_path = config.detector_model_path_utf8;
    if (config.pose_model_path_utf8 != nullptr) {
        copy.pose_model_path = config.pose_model_path_utf8;
    }
    return copy;
}

HumanVisionEngine::HumanVisionEngine(const HV_Config& config)
    : config_(CopyConfig(config)) {}

HumanVisionEngine::~HumanVisionEngine() {
    frame_slot_.Stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool HumanVisionEngine::Initialize(std::string& error) {
    auto detector = std::make_unique<RtmdetModel>(
        std::make_unique<OnnxRuntimeBackend>());
    const std::filesystem::path model_path =
        std::filesystem::u8path(config_.detector_model_path);
    if (!detector->Load(model_path, error)) {
        SetLastError(error);
        return false;
    }
    detector_ = std::move(detector);
    auto pose = std::make_unique<RtmposeModel>(
        std::make_unique<OnnxRuntimeBackend>());
    const std::filesystem::path pose_model_path =
        std::filesystem::u8path(config_.pose_model_path);
    if (!pose->Load(pose_model_path, error)) {
        SetLastError(error);
        return false;
    }
    pose_ = std::move(pose);
    tracker_ = std::make_unique<CenterIouTracker>();
    worker_ = std::thread(&HumanVisionEngine::WorkerLoop, this);
    error.clear();
    return true;
}

HV_Result HumanVisionEngine::Reconfigure(const HV_Config& config) {
    std::string error;
    const HV_Result validation = ValidateConfig(&config, error);
    if (validation != HV_OK) {
        SetLastError(std::move(error));
        return validation;
    }
    RuntimeConfig replacement = CopyConfig(config);
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (replacement.detector_model_path != config_.detector_model_path) {
        SetLastError(
            "changing detector_model_path requires destroy/create during D0");
        return HV_ERR_INVALID_ARGUMENT;
    }
    if (replacement.pose_model_path != config_.pose_model_path) {
        SetLastError("changing pose_model_path requires destroy/create during D0");
        return HV_ERR_INVALID_ARGUMENT;
    }
    config_ = std::move(replacement);
    return HV_OK;
}

HV_Result HumanVisionEngine::SubmitFrame(const HV_VideoFrame* frame) {
    std::string error;
    const HV_Result validation = ValidateVideoFrame(frame, error);
    if (validation != HV_OK) {
        SetLastError(std::move(error));
        return validation;
    }
    FrameSubmitStatus status{};
    if (!frame_slot_.Submit(*frame, status, error)) {
        SetLastError(std::move(error));
        return HV_ERR_INTERNAL;
    }
    stats_.RecordSubmitted(status == FrameSubmitStatus::kReplacedPending);
    return HV_OK;
}

HV_Result HumanVisionEngine::GetLatestResultMeta(HV_ResultMeta* destination) const {
    if (destination == nullptr) {
        return HV_ERR_INVALID_ARGUMENT;
    }
    return result_store_.GetMeta(*destination);
}

int HumanVisionEngine::GetBodyCount() const {
    return result_store_.BodyCount();
}

HV_Result HumanVisionEngine::GetBodies(
    HV_Body* destination,
    const int capacity,
    int* written) const {
    return result_store_.CopyBodies(destination, capacity, written);
}

HV_Result HumanVisionEngine::GetStats(HV_Stats* destination) const {
    if (destination == nullptr ||
        destination->struct_size < static_cast<std::int32_t>(sizeof(HV_Stats))) {
        return HV_ERR_INVALID_ARGUMENT;
    }
    *destination = stats_.Snapshot(frame_slot_.dropped_frames());
    return HV_OK;
}

std::string HumanVisionEngine::LastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void HumanVisionEngine::SetLastError(std::string error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = std::move(error);
}

void HumanVisionEngine::WorkerLoop() {
    FrameBuffer frame;
    std::vector<Detection> detections;
    std::vector<TrackedDetection> tracked_detections;
    ResultSnapshot snapshot;
    bool tracking_state_initialized = false;
    bool previous_tracking_state = false;
    while (frame_slot_.WaitTake(frame)) {
        int max_bodies = 0;
        float detection_threshold = 0.0F;
        float pose_threshold = 0.0F;
        int detection_interval = 1;
        bool enable_tracking = false;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            max_bodies = config_.max_bodies;
            detection_threshold = config_.detection_threshold;
            pose_threshold = config_.pose_threshold;
            detection_interval = config_.detection_interval;
            enable_tracking = config_.enable_tracking;
        }
        if (!tracking_state_initialized ||
            enable_tracking != previous_tracking_state) {
            tracker_->Reset();
            previous_tracking_state = enable_tracking;
            tracking_state_initialized = true;
            processed_input_frames_ = 0;
        }

        const auto total_start = std::chrono::steady_clock::now();
        StageTimings timings;
        std::string error;
        const bool run_detector = !enable_tracking || processed_input_frames_ == 0 ||
                                  processed_input_frames_ % detection_interval == 0;
        if (run_detector) {
            const auto detection_start = std::chrono::steady_clock::now();
            detections.clear();
            float detector_inference_ms = 0.0F;
            if (!detector_->Detect(
                    frame,
                    detection_threshold,
                    max_bodies,
                    detections,
                    detector_inference_ms,
                    error)) {
                SetLastError(std::move(error));
                continue;
            }
            timings.detection_ms = std::chrono::duration<float, std::milli>(
                                       std::chrono::steady_clock::now() -
                                       detection_start)
                                       .count();
        }

        const auto tracking_start = std::chrono::steady_clock::now();
        if (enable_tracking) {
            if (run_detector) {
                tracker_->Update(detections, frame.timestamp_us, tracked_detections);
            } else {
                tracker_->Predict(frame.timestamp_us, tracked_detections);
            }
        } else {
            tracked_detections.clear();
            tracked_detections.reserve(detections.size());
            for (const Detection& detection : detections) {
                tracked_detections.push_back(TrackedDetection{detection, -1});
            }
        }
        if (tracked_detections.size() > static_cast<std::size_t>(max_bodies)) {
            tracked_detections.resize(static_cast<std::size_t>(max_bodies));
        }
        timings.tracking_ms = std::chrono::duration<float, std::milli>(
                                  std::chrono::steady_clock::now() - tracking_start)
                                  .count();

        snapshot.bodies.clear();
        snapshot.meta.struct_size = sizeof(HV_ResultMeta);
        snapshot.meta.result_sequence = ++result_sequence_;
        snapshot.meta.source_frame_id = frame.frame_id;
        snapshot.meta.source_timestamp_us = frame.timestamp_us;
        snapshot.bodies.reserve(tracked_detections.size());
        const auto pose_start = std::chrono::steady_clock::now();
        bool pose_succeeded = true;
        for (const TrackedDetection& tracked : tracked_detections) {
            HV_Body body{};
            body.struct_size = sizeof(HV_Body);
            body.track_id = tracked.track_id;
            const Detection& detection = tracked.detection;
            body.bbox_px.x = detection.x1;
            body.bbox_px.y = detection.y1;
            body.bbox_px.width = std::max(0.0F, detection.x2 - detection.x1);
            body.bbox_px.height = std::max(0.0F, detection.y2 - detection.y1);
            body.detection_confidence = detection.score;
            std::array<HV_Joint, HV_JOINT_COUNT> joints{};
            float pose_inference_ms = 0.0F;
            if (!pose_->Estimate(
                    frame,
                    detection,
                    pose_threshold,
                    joints,
                    pose_inference_ms,
                    error)) {
                pose_succeeded = false;
                break;
            }
            std::copy(joints.begin(), joints.end(), std::begin(body.joints));
            snapshot.bodies.push_back(body);
        }
        if (!pose_succeeded) {
            SetLastError(std::move(error));
            continue;
        }
        timings.pose_ms = std::chrono::duration<float, std::milli>(
                              std::chrono::steady_clock::now() - pose_start)
                              .count();
        timings.total_ms = std::chrono::duration<float, std::milli>(
                               std::chrono::steady_clock::now() - total_start)
                               .count();
        ++processed_input_frames_;
        stats_.RecordProcessed(timings);
        result_store_.Publish(snapshot);
    }
}

}  // namespace humanvision
