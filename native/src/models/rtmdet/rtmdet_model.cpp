#include "models/rtmdet/rtmdet_model.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace humanvision {

namespace {

constexpr int kInputWidth = 640;
constexpr int kInputHeight = 640;
constexpr std::array<float, 3> kMean = {103.53F, 116.28F, 123.675F};
constexpr std::array<float, 3> kStd = {57.375F, 57.12F, 58.395F};
constexpr std::uint8_t kPadValue = 114;

std::array<std::uint8_t, 3> ReadBgr(
    const FrameBuffer& frame,
    const int x,
    const int y) {
    const int bytes_per_pixel = BytesPerPixel(frame.pixel_format);
    const std::size_t offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.stride_bytes) +
        static_cast<std::size_t>(x) * static_cast<std::size_t>(bytes_per_pixel);
    const auto* pixel = frame.bytes.data() + offset;
    switch (frame.pixel_format) {
        case HV_PIXEL_BGR24:
        case HV_PIXEL_BGRA32:
            return {pixel[0], pixel[1], pixel[2]};
        case HV_PIXEL_RGB24:
        case HV_PIXEL_RGBA32:
            return {pixel[2], pixel[1], pixel[0]};
        default:
            return {0, 0, 0};
    }
}

std::uint8_t BilinearChannel(
    const FrameBuffer& frame,
    const int destination_x,
    const int destination_y,
    const int resized_width,
    const int resized_height,
    const int channel) {
    const float source_x =
        (static_cast<float>(destination_x) + 0.5F) * frame.width / resized_width -
        0.5F;
    const float source_y =
        (static_cast<float>(destination_y) + 0.5F) * frame.height / resized_height -
        0.5F;
    const int x0_unclamped = static_cast<int>(std::floor(source_x));
    const int y0_unclamped = static_cast<int>(std::floor(source_y));
    const int x0 = std::clamp(x0_unclamped, 0, frame.width - 1);
    const int y0 = std::clamp(y0_unclamped, 0, frame.height - 1);
    const int x1 = std::clamp(x0_unclamped + 1, 0, frame.width - 1);
    const int y1 = std::clamp(y0_unclamped + 1, 0, frame.height - 1);
    const float fraction_x = std::clamp(source_x - x0_unclamped, 0.0F, 1.0F);
    const float fraction_y = std::clamp(source_y - y0_unclamped, 0.0F, 1.0F);
    const auto top_left = ReadBgr(frame, x0, y0);
    const auto top_right = ReadBgr(frame, x1, y0);
    const auto bottom_left = ReadBgr(frame, x0, y1);
    const auto bottom_right = ReadBgr(frame, x1, y1);
    const float top = top_left[static_cast<std::size_t>(channel)] * (1.0F - fraction_x) +
                      top_right[static_cast<std::size_t>(channel)] * fraction_x;
    const float bottom =
        bottom_left[static_cast<std::size_t>(channel)] * (1.0F - fraction_x) +
        bottom_right[static_cast<std::size_t>(channel)] * fraction_x;
    const float value = top * (1.0F - fraction_y) + bottom * fraction_y;
    return static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

const Tensor* FindOutput(const std::vector<Tensor>& outputs, const std::string& name) {
    const auto iterator = std::find_if(
        outputs.begin(), outputs.end(), [&name](const Tensor& value) {
            return value.name == name;
        });
    return iterator == outputs.end() ? nullptr : &*iterator;
}

}  // namespace

RtmdetModel::RtmdetModel(std::unique_ptr<IInferenceBackend> backend)
    : backend_(std::move(backend)) {
    tensor_input_.name = "input";
    tensor_input_.shape = {1, 3, kInputHeight, kInputWidth};
}

bool RtmdetModel::Load(
    const std::filesystem::path& model_path,
    std::string& error) {
    if (!backend_) {
        error = "RTMDet inference backend is null";
        return false;
    }
    return backend_->Load(model_path, error);
}

bool RtmdetModel::Preprocess(
    const FrameBuffer& frame,
    DetectorInput& destination,
    std::string& error) const {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.stride_bytes < frame.width * BytesPerPixel(frame.pixel_format) ||
        frame.bytes.size() <
            static_cast<std::size_t>(frame.stride_bytes) * frame.height) {
        error = "invalid owned frame for RTMDet preprocessing";
        return false;
    }
    if (BytesPerPixel(frame.pixel_format) == 0) {
        error = "unsupported pixel format for RTMDet preprocessing";
        return false;
    }

    const float resize_ratio = std::min(
        static_cast<float>(kInputWidth) / frame.width,
        static_cast<float>(kInputHeight) / frame.height);
    destination.resized_width =
        std::max(1, static_cast<int>(frame.width * resize_ratio + 0.5F));
    destination.resized_height =
        std::max(1, static_cast<int>(frame.height * resize_ratio + 0.5F));
    destination.scale_x = static_cast<float>(destination.resized_width) / frame.width;
    destination.scale_y = static_cast<float>(destination.resized_height) / frame.height;

    const std::size_t plane_size =
        static_cast<std::size_t>(kInputWidth) * kInputHeight;
    destination.normalized_chw.resize(plane_size * 3U);
    for (int channel = 0; channel < 3; ++channel) {
        const float normalized_pad = (kPadValue - kMean[channel]) / kStd[channel];
        std::fill_n(
            destination.normalized_chw.data() +
                static_cast<std::size_t>(channel) * plane_size,
            plane_size,
            normalized_pad);
    }

    for (int y = 0; y < destination.resized_height; ++y) {
        for (int x = 0; x < destination.resized_width; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                const std::uint8_t value = BilinearChannel(
                    frame,
                    x,
                    y,
                    destination.resized_width,
                    destination.resized_height,
                    channel);
                const std::size_t index =
                    static_cast<std::size_t>(channel) * plane_size +
                    static_cast<std::size_t>(y) * kInputWidth + x;
                destination.normalized_chw[index] =
                    (static_cast<float>(value) - kMean[channel]) / kStd[channel];
            }
        }
    }
    error.clear();
    return true;
}

bool RtmdetModel::Detect(
    const FrameBuffer& frame,
    const float threshold,
    const int max_bodies,
    std::vector<Detection>& detections,
    float& inference_ms,
    std::string& error) {
    if (threshold < 0.0F || threshold > 1.0F || max_bodies < 1) {
        error = "invalid RTMDet threshold or max_bodies";
        return false;
    }
    if (!Preprocess(frame, input_buffer_, error)) {
        return false;
    }
    tensor_input_.values.swap(input_buffer_.normalized_chw);
    const auto start = std::chrono::steady_clock::now();
    const bool run_succeeded = backend_->Run(tensor_input_, output_buffers_, error);
    tensor_input_.values.swap(input_buffer_.normalized_chw);
    if (!run_succeeded) {
        return false;
    }
    inference_ms = std::chrono::duration<float, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    const Tensor* boxes = FindOutput(output_buffers_, "dets");
    const Tensor* labels = FindOutput(output_buffers_, "labels");
    if (boxes == nullptr || labels == nullptr || boxes->values.size() % 5U != 0U) {
        error = "RTMDet outputs must contain float tensors named dets [N,5] and labels [N]";
        return false;
    }
    const std::size_t count = boxes->values.size() / 5U;
    if (labels->values.size() < count) {
        error = "RTMDet labels output is shorter than detections output";
        return false;
    }

    detections.clear();
    detections.reserve(std::min<std::size_t>(count, static_cast<std::size_t>(max_bodies)));
    for (std::size_t index = 0; index < count; ++index) {
        const float* row = boxes->values.data() + index * 5U;
        const int label = static_cast<int>(std::lround(labels->values[index]));
        if (label != 0 || row[4] < threshold) {
            continue;
        }
        Detection detection;
        detection.x1 = std::clamp(row[0] / input_buffer_.scale_x, 0.0F, static_cast<float>(frame.width));
        detection.y1 = std::clamp(row[1] / input_buffer_.scale_y, 0.0F, static_cast<float>(frame.height));
        detection.x2 = std::clamp(row[2] / input_buffer_.scale_x, 0.0F, static_cast<float>(frame.width));
        detection.y2 = std::clamp(row[3] / input_buffer_.scale_y, 0.0F, static_cast<float>(frame.height));
        detection.score = row[4];
        detections.push_back(detection);
    }
    std::sort(
        detections.begin(), detections.end(), [](const Detection& left, const Detection& right) {
            return left.score > right.score;
        });
    if (detections.size() > static_cast<std::size_t>(max_bodies)) {
        detections.resize(static_cast<std::size_t>(max_bodies));
    }
    error.clear();
    return true;
}

}  // namespace humanvision
