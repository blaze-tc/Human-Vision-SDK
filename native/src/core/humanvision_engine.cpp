#include "core/humanvision_engine.h"

#include "backend/onnx/onnx_runtime_backend.h"
#include "tracking/center_iou_tracker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace humanvision {

namespace {
int RegionAt(const std::vector<HV_Rect>& regions, float x, float y) {
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        if (x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height)
            return static_cast<int>(i);
    }
    return -1;
}

void MaskOutsideRegions(FrameBuffer& frame, const std::vector<HV_Rect>& regions) {
    const int bpp = BytesPerPixel(frame.pixel_format);
    for (int y = 0; y < frame.height; ++y) {
        auto* row = frame.bytes.data() + static_cast<size_t>(y) * frame.stride_bytes;
        for (int x = 0; x < frame.width; ++x) {
            if (RegionAt(regions, (x + .5F) / frame.width, (y + .5F) / frame.height) < 0) {
                std::memset(row + x * bpp, 0, bpp);
                if (bpp == 4) row[x * bpp + 3] = 255;
            }
        }
    }
}
}

HV_Result HumanVisionEngine::SetRegions(const HV_Rect* regions, int count, int64_t revision) {
    if (count < 0 || (count && !regions)) return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(config_mutex_);
    if (count && count != config_.max_bodies) {
        SetLastError("Region count must equal MaxBodies"); return HV_ERR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < count; ++i) {
        const auto& r = regions[i];
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height) ||
            r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 || r.x + r.width > 1.000001F || r.y + r.height > 1.000001F) {
            SetLastError("Regions must be positive normalized rectangles within the image"); return HV_ERR_INVALID_ARGUMENT;
        }
        for (int j = 0; j < i; ++j) {
            const auto& q = regions[j];
            if (std::min(r.x + r.width, q.x + q.width) - std::max(r.x, q.x) > .000001F &&
                std::min(r.y + r.height, q.y + q.height) - std::max(r.y, q.y) > .000001F) {
                SetLastError("Recognition regions must not overlap"); return HV_ERR_INVALID_ARGUMENT;
            }
        }
    }
    regions_.clear();
    if (count) regions_.assign(regions, regions + count);
    region_revision_ = revision;
    return HV_OK;
}

HV_Result HumanVisionEngine::GetRegionAssignments(int64_t sequence, int32_t* indices, int capacity, int64_t* revision) const {
    return result_store_.CopyRegions(sequence, indices, capacity, revision);
}

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
    copy.backend = config.backend;
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
#if defined(__ANDROID__)
    { std::lock_guard<std::mutex> lock(detector_mutex_); detector_stop_ = true; }
    detector_condition_.notify_one();
    if (detector_worker_.joinable()) detector_worker_.join();
#endif
}

bool HumanVisionEngine::Initialize(std::string& error) {
    bool use_gpu = false;
#if defined(HV_USE_DIRECTML) || defined(__ANDROID__)
    use_gpu = config_.backend == HV_BACKEND_AUTO;
#endif
    auto detector = std::make_unique<RtmdetModel>(
#if defined(__ANDROID__)
        // The supplied device measurements show the NNAPI detector regressing
        // to ~1080 ms. Keep acceleration for pose, use ORT CPU for detection.
        std::make_unique<OnnxRuntimeBackend>(false));
#else
        std::make_unique<OnnxRuntimeBackend>(use_gpu));
#endif
    const std::filesystem::path model_path =
        std::filesystem::u8path(config_.detector_model_path);
    if (!detector->Load(model_path, error)) {
        SetLastError(error);
        return false;
    }
    detector_ = std::move(detector);
    auto pose = std::make_unique<RtmposeModel>(
        std::make_unique<OnnxRuntimeBackend>(use_gpu));
    const std::filesystem::path pose_model_path =
        std::filesystem::u8path(config_.pose_model_path);
    if (!pose->Load(pose_model_path, error)) {
        SetLastError(error);
        return false;
    }
    pose_ = std::move(pose);
    tracker_ = std::make_unique<CenterIouTracker>();
#if defined(__ANDROID__)
    detector_worker_ = std::thread(&HumanVisionEngine::DetectorLoop, this);
#endif
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
    if (replacement.backend != config_.backend) {
        SetLastError("changing backend requires destroy/create");
        return HV_ERR_INVALID_ARGUMENT;
    }
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

#if defined(__ANDROID__)
void HumanVisionEngine::DetectorLoop() {
    FrameBuffer frame;
    RuntimeConfig config;
    std::vector<HV_Rect> regions;
    std::vector<Detection> detections, selected;
    std::vector<bool> occupied;
    while (true) {
        int64_t revision;
        {
            std::unique_lock<std::mutex> lock(detector_mutex_);
            detector_condition_.wait(lock, [this] { return detector_stop_ || detector_pending_; });
            if (detector_stop_) return;
            std::swap(frame, detector_frame_);
            config = detector_config_;
            regions.assign(detector_regions_.begin(), detector_regions_.end());
            revision = detector_request_revision_;
            detector_pending_ = false;
        }
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        float inference_ms = 0;
        detections.clear();
        if (!detector_->Detect(frame, config.detection_threshold,
                regions.empty() ? config.max_bodies : 300, detections, inference_ms, error)) {
            SetLastError(std::move(error));
            // A failed detection cannot keep earlier crops alive indefinitely.
            detections.clear();
        }
        if (!regions.empty()) {
            selected.clear(); occupied.assign(regions.size(), false);
            for (const auto& detection : detections) {
                int region = RegionAt(regions, (detection.x1 + detection.x2) * .5F / frame.width,
                    (detection.y1 + detection.y2) * .5F / frame.height);
                if (region >= 0 && !occupied[region]) {
                    occupied[region] = true; selected.push_back(detection);
                }
            }
            detections.assign(selected.begin(), selected.end());
        }
        const float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
        {
            std::lock_guard<std::mutex> lock(detector_mutex_);
            detector_result_.detections.assign(detections.begin(), detections.end());
            detector_result_.width = frame.width; detector_result_.height = frame.height;
            detector_result_.revision = revision; detector_result_.timestamp_us = frame.timestamp_us;
            detector_result_.elapsed_ms = elapsed; ++detector_result_.sequence;
        }
    }
}
#endif

void HumanVisionEngine::WorkerLoop() {
    FrameBuffer frame;
    std::vector<Detection> detections;
    std::vector<TrackedDetection> tracked_detections;
    std::vector<HV_Rect> regions;
    std::vector<Detection> selected;
    std::vector<bool> occupied;
    int64_t previous_region_revision = -1;
    ResultSnapshot snapshot;
    bool tracking_state_initialized = false;
    bool previous_tracking_state = false;
    int previous_width = 0, previous_height = 0;
    int64_t last_detection_timestamp = 0;
#if defined(__ANDROID__)
    int64_t detector_sequence = -1, last_detector_submit = -1;
    bool detector_ready = false;
#endif
    while (frame_slot_.WaitTake(frame)) {
        int max_bodies = 0;
        float detection_threshold = 0.0F;
        float pose_threshold = 0.0F;
        int detection_interval = 1;
        bool enable_tracking = false;
        int64_t region_revision;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            max_bodies = config_.max_bodies;
            detection_threshold = config_.detection_threshold;
            pose_threshold = config_.pose_threshold;
            detection_interval = config_.detection_interval;
            enable_tracking = config_.enable_tracking;
            regions.assign(regions_.begin(), regions_.end());
            region_revision = region_revision_;
        }
        if (!tracking_state_initialized ||
            enable_tracking != previous_tracking_state || previous_region_revision != region_revision ||
            previous_width != frame.width || previous_height != frame.height) {
            tracker_->Reset();
            previous_tracking_state = enable_tracking;
            tracking_state_initialized = true;
            processed_input_frames_ = 0;
            previous_region_revision = region_revision;
            previous_width = frame.width; previous_height = frame.height;
            tracked_detections.clear(); last_detection_timestamp = 0;
#if defined(__ANDROID__)
            detector_sequence = -1; last_detector_submit = -1;
#endif
        }

        const auto total_start = std::chrono::steady_clock::now();
        StageTimings timings;
        std::string error;
        if (!regions.empty()) MaskOutsideRegions(frame, regions);
#if defined(__ANDROID__)
        bool run_detector = false;
        {
            std::lock_guard<std::mutex> lock(detector_mutex_);
            // Refresh the pending detector image at 5 Hz. Detector inference never
            // holds this mutex and never blocks the pose worker.
            if (last_detector_submit < 0 || frame.timestamp_us < last_detector_submit ||
                    frame.timestamp_us - last_detector_submit >= 200000) {
                detector_frame_ = frame;
                detector_config_.max_bodies = max_bodies;
                detector_config_.detection_threshold = detection_threshold;
                detector_regions_.assign(regions.begin(), regions.end());
                detector_request_revision_ = region_revision;
                detector_pending_ = true;
                last_detector_submit = frame.timestamp_us;
                detector_condition_.notify_one();
            }
            const auto& result = detector_result_;
            detector_ready = result.sequence > 0 && result.revision == region_revision &&
                result.width == frame.width && result.height == frame.height &&
                frame.timestamp_us >= result.timestamp_us && frame.timestamp_us - result.timestamp_us <= 1200000;
            timings.detection_ms = result.elapsed_ms;
            if (detector_ready && result.sequence != detector_sequence) {
                detections.assign(result.detections.begin(), result.detections.end());
                detector_sequence = result.sequence;
                last_detection_timestamp = result.timestamp_us;
                run_detector = true;
            }
        }
        if (!detector_ready) { tracker_->Reset(); detections.clear(); tracked_detections.clear(); detector_sequence = -1; }
#else
        // Pose (including hands) still runs on every processed frame. Reuse only
        // the short-lived tracked crop between detector passes, even with masks.
        const bool run_detector = !enable_tracking || tracked_detections.empty() || processed_input_frames_ == 0 ||
                                  frame.timestamp_us - last_detection_timestamp >= 500000 ||
                                  frame.timestamp_us < last_detection_timestamp ||
                                  processed_input_frames_ % detection_interval == 0;
        if (run_detector) {
            const auto detection_start = std::chrono::steady_clock::now();
            detections.clear();
            float detector_inference_ms = 0.0F;
            if (!detector_->Detect(
                    frame,
                    detection_threshold,
                    regions.empty() ? max_bodies : 300,
                    detections,
                    detector_inference_ms,
                    error)) {
                SetLastError(std::move(error));
                continue;
            }
            if (!regions.empty()) {
                selected.clear();
                occupied.assign(regions.size(), false);
                for (const auto& detection : detections) {
                    const int region = RegionAt(regions, (detection.x1 + detection.x2) * .5F / frame.width,
                        (detection.y1 + detection.y2) * .5F / frame.height);
                    if (region >= 0 && !occupied[region]) {
                        occupied[region] = true;
                        selected.push_back(detection);
                    }
                }
                detections.assign(selected.begin(), selected.end());
            }
            timings.detection_ms = std::chrono::duration<float, std::milli>(
                                       std::chrono::steady_clock::now() -
                                       detection_start)
                                       .count();
            last_detection_timestamp = frame.timestamp_us;
        }

#endif

        const auto tracking_start = std::chrono::steady_clock::now();
        if (enable_tracking) {
            if (run_detector) {
                #if defined(__ANDROID__)
                tracker_->Update(detections, last_detection_timestamp, tracked_detections);
                tracker_->Predict(frame.timestamp_us, tracked_detections);
#else
                tracker_->Update(detections, frame.timestamp_us, tracked_detections);
#endif
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
        snapshot.hands.clear();
        snapshot.hands.reserve(tracked_detections.size() * 6);
        snapshot.region_indices.clear();
        snapshot.region_revision = region_revision;
        snapshot.meta.struct_size = sizeof(HV_ResultMeta);
        snapshot.meta.result_sequence = ++result_sequence_;
        snapshot.meta.source_frame_id = frame.frame_id;
        snapshot.meta.source_timestamp_us = frame.timestamp_us;
        snapshot.bodies.reserve(tracked_detections.size());
        const auto pose_start = std::chrono::steady_clock::now();
        bool pose_succeeded = true;
        occupied.assign(regions.size(), false);
        for (const TrackedDetection& tracked : tracked_detections) {
            HV_Body body{};
            body.struct_size = sizeof(HV_Body);
            body.track_id = tracked.track_id;
            const Detection& detection = tracked.detection;
            const int region = RegionAt(regions, (detection.x1 + detection.x2) * .5F / frame.width,
                (detection.y1 + detection.y2) * .5F / frame.height);
            if (!regions.empty() && region < 0) continue;
            // Predicted crops may cross region boundaries between detector results.
            // Keep the public one-body-per-region contract in the pose snapshot too.
            if (region >= 0) {
                if (occupied[region]) continue;
                occupied[region] = true;
            }
            body.bbox_px.x = detection.x1;
            body.bbox_px.y = detection.y1;
            body.bbox_px.width = std::max(0.0F, detection.x2 - detection.x1);
            body.bbox_px.height = std::max(0.0F, detection.y2 - detection.y1);
            body.detection_confidence = detection.score;
            std::array<HV_Joint, HV_JOINT_COUNT> joints{};
            std::array<HV_Joint, 6> hands{};
            float pose_inference_ms = 0.0F;
            if (!pose_->Estimate(
                    frame,
                    detection,
                    pose_threshold,
                    joints,
                    pose_inference_ms,
                    error, &hands)) {
                pose_succeeded = false;
                break;
            }
            std::copy(joints.begin(), joints.end(), std::begin(body.joints));
            // Do not expose inferred landmarks in areas explicitly excluded by the user.
            if (!regions.empty()) {
                for (auto& joint : body.joints) if (RegionAt(regions, joint.x_norm, joint.y_norm) != region) joint.valid = 0;
                for (auto& joint : hands) if (RegionAt(regions, joint.x_norm, joint.y_norm) != region) joint.valid = 0;
            }
            snapshot.bodies.push_back(body);
            snapshot.hands.insert(snapshot.hands.end(), hands.begin(), hands.end());
            snapshot.region_indices.push_back(region);
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
#if defined(__ANDROID__)
        // Polling for a first detection is not a completed inference.
        if (detector_ready && (!snapshot.bodies.empty() || run_detector)) stats_.RecordProcessed(timings);
        else stats_.RecordTimings(timings);
#else
        stats_.RecordProcessed(timings);
#endif
        result_store_.Publish(snapshot);
    }
}

}  // namespace humanvision
