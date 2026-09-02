#include "core/humanvision_engine.h"

#include "backend/onnx/onnx_runtime_backend.h"

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
    : config_(CopyConfig(config)), started_at_(std::chrono::steady_clock::now()) {
    stats_.struct_size = sizeof(HV_Stats);
}

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
            "changing detector_model_path requires destroy/create during D0.2");
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
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++stats_.submitted_frames;
        stats_.dropped_frames = frame_slot_.dropped_frames();
    }
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
    std::lock_guard<std::mutex> lock(stats_mutex_);
    *destination = stats_;
    destination->dropped_frames = frame_slot_.dropped_frames();
    const float elapsed_seconds =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - started_at_)
            .count();
    if (elapsed_seconds > 0.0F) {
        destination->input_fps =
            static_cast<float>(destination->submitted_frames) / elapsed_seconds;
        destination->inference_fps =
            static_cast<float>(destination->processed_frames) / elapsed_seconds;
    }
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
    ResultSnapshot snapshot;
    while (frame_slot_.WaitTake(frame)) {
        int max_bodies = 0;
        float detection_threshold = 0.0F;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            max_bodies = config_.max_bodies;
            detection_threshold = config_.detection_threshold;
        }

        const auto total_start = std::chrono::steady_clock::now();
        detections.clear();
        float inference_ms = 0.0F;
        std::string error;
        if (!detector_->Detect(
                frame,
                detection_threshold,
                max_bodies,
                detections,
                inference_ms,
                error)) {
            SetLastError(std::move(error));
            continue;
        }

        snapshot.bodies.clear();
        snapshot.meta.struct_size = sizeof(HV_ResultMeta);
        snapshot.meta.result_sequence = ++result_sequence_;
        snapshot.meta.source_frame_id = frame.frame_id;
        snapshot.meta.source_timestamp_us = frame.timestamp_us;
        snapshot.bodies.reserve(detections.size());
        for (const Detection& detection : detections) {
            HV_Body body{};
            body.struct_size = sizeof(HV_Body);
            body.track_id = -1;
            body.bbox_px.x = detection.x1;
            body.bbox_px.y = detection.y1;
            body.bbox_px.width = std::max(0.0F, detection.x2 - detection.x1);
            body.bbox_px.height = std::max(0.0F, detection.y2 - detection.y1);
            body.detection_confidence = detection.score;
            snapshot.bodies.push_back(body);
        }
        const float total_ms = std::chrono::duration<float, std::milli>(
                                   std::chrono::steady_clock::now() - total_start)
                                   .count();
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.processed_frames;
            stats_.detection_ms = inference_ms;
            stats_.total_ms = total_ms;
            stats_.dropped_frames = frame_slot_.dropped_frames();
        }
        result_store_.Publish(snapshot);
    }
}

}  // namespace humanvision
