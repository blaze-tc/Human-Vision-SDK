#include "humanvision/humanvision_c.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path detector_model;
    std::filesystem::path pose_model;
    std::filesystem::path output_prefix;
    int width = 0;
    int height = 0;
    int fps = 5;
    int frames = 10;
    int max_bodies = 4;
    HV_Backend backend = HV_BACKEND_ONNX_CPU;
};

struct FrameResult {
    std::int64_t frame_id = 0;
    HV_Stats stats{};
    std::vector<HV_Body> bodies;
};

std::string JsonEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (const char value : input) {
        switch (value) {
            case '\\':
                output += "\\\\";
                break;
            case '"':
                output += "\\\"";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += value;
                break;
        }
    }
    return output;
}

int ParsePositiveInt(const char* text, const std::string& option) {
    try {
        const int value = std::stoi(text);
        if (value <= 0) {
            throw std::invalid_argument("not positive");
        }
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error(option + " requires a positive integer");
    }
}

Options ParseOptions(const int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + option);
        }
        const char* value = argv[++index];
        if (option == "--input") {
            options.input = std::filesystem::u8path(value);
        } else if (option == "--detector-model") {
            options.detector_model = std::filesystem::u8path(value);
        } else if (option == "--pose-model") {
            options.pose_model = std::filesystem::u8path(value);
        } else if (option == "--output-prefix") {
            options.output_prefix = std::filesystem::u8path(value);
        } else if (option == "--width") {
            options.width = ParsePositiveInt(value, option);
        } else if (option == "--height") {
            options.height = ParsePositiveInt(value, option);
        } else if (option == "--fps") {
            options.fps = ParsePositiveInt(value, option);
        } else if (option == "--frames") {
            options.frames = ParsePositiveInt(value, option);
        } else if (option == "--backend") {
            const std::string backend(value);
            if (backend != "cpu" && backend != "auto") {
                throw std::runtime_error("--backend requires cpu or auto");
            }
            options.backend = backend == "auto" ? HV_BACKEND_AUTO : HV_BACKEND_ONNX_CPU;
        } else if (option == "--max-bodies") {
            options.max_bodies = ParsePositiveInt(value, option);
        } else {
            throw std::runtime_error("unknown option: " + option);
        }
    }
    if (!std::filesystem::is_regular_file(options.input) ||
        !std::filesystem::is_regular_file(options.detector_model) ||
        !std::filesystem::is_regular_file(options.pose_model) ||
        options.output_prefix.empty() || options.width <= 0 || options.height <= 0) {
        throw std::runtime_error(
            "required: --input FILE --width N --height N --detector-model FILE "
            "--pose-model FILE --output-prefix PATH");
    }
    return options;
}

std::string QuoteForCommand(const std::filesystem::path& path) {
    const std::string value = path.string();
    if (value.find('"') != std::string::npos) {
        throw std::runtime_error("input path contains an unsupported quote character");
    }
    return '"' + value + '"';
}

class Pipe {
public:
    Pipe(const std::filesystem::path& input, const int frames) {
        const std::string command =
            "ffmpeg -v error -i " + QuoteForCommand(input) +
            " -an -sn -frames:v " + std::to_string(frames) +
            " -f rawvideo -pix_fmt bgr24 - 2>NUL";
        stream_ = _popen(command.c_str(), "rb");
        if (stream_ == nullptr) {
            throw std::runtime_error("could not start ffmpeg; ensure it is on PATH");
        }
    }

    ~Pipe() {
        if (stream_ != nullptr) {
            _pclose(stream_);
        }
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    bool ReadFrame(std::vector<std::uint8_t>& destination) {
        std::size_t consumed = 0;
        while (consumed < destination.size()) {
            const std::size_t count = std::fread(
                destination.data() + consumed,
                1,
                destination.size() - consumed,
                stream_);
            if (count == 0) {
                if (consumed == 0 && std::feof(stream_) != 0) {
                    return false;
                }
                throw std::runtime_error("ffmpeg produced a truncated raw frame");
            }
            consumed += count;
        }
        return true;
    }

    int Close() {
        if (stream_ == nullptr) {
            return 0;
        }
        FILE* stream = stream_;
        stream_ = nullptr;
        return _pclose(stream);
    }

private:
    FILE* stream_ = nullptr;
};

class Handle {
public:
    ~Handle() {
        HV_Destroy(value_);
    }

    HV_Handle* Out() {
        return &value_;
    }

    operator HV_Handle() const {
        return value_;
    }

private:
    HV_Handle value_ = nullptr;
};

FrameResult SubmitAndWait(
    const Handle& handle,
    const Options& options,
    const std::vector<std::uint8_t>& pixels,
    const std::int64_t frame_id) {
    HV_VideoFrame frame{};
    frame.struct_size = sizeof(HV_VideoFrame);
    frame.width = options.width;
    frame.height = options.height;
    frame.stride_bytes = options.width * 3;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.frame_id = frame_id;
    frame.timestamp_us = frame_id * 1000000LL / options.fps;
    frame.data = pixels.data();
    frame.data_bytes = static_cast<std::int32_t>(pixels.size());
    const HV_Result submitted = HV_SubmitFrame(handle, &frame);
    if (submitted != HV_OK) {
        throw std::runtime_error(
            std::string("HV_SubmitFrame failed: ") + HV_GetLastError(handle));
    }

    HV_ResultMeta meta{};
    meta.struct_size = sizeof(HV_ResultMeta);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        const HV_Result result = HV_GetLatestResultMeta(handle, &meta);
        if (result == HV_OK && meta.source_frame_id == frame_id) {
            FrameResult frame_result;
            frame_result.frame_id = frame_id;
            frame_result.bodies.resize(static_cast<std::size_t>(meta.body_count));
            int written = 0;
            if (HV_GetBodies(
                    handle,
                    frame_result.bodies.data(),
                    meta.body_count,
                    &written) != HV_OK ||
                written != meta.body_count) {
                throw std::runtime_error("HV_GetBodies failed for completed frame");
            }
            frame_result.stats.struct_size = sizeof(HV_Stats);
            if (HV_GetStats(handle, &frame_result.stats) != HV_OK) {
                throw std::runtime_error("HV_GetStats failed for completed frame");
            }
            return frame_result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error(
        std::string("timed out waiting for frame: ") + HV_GetLastError(handle));
}

void WriteCsv(
    const std::filesystem::path& path,
    const std::vector<FrameResult>& frames) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write CSV: " + path.string());
    }
    output << "frame_id,body_count,detection_ms,pose_ms,tracking_ms,total_ms,"
              "submitted_frames,processed_frames,dropped_frames\n";
    output << std::fixed << std::setprecision(6);
    for (const auto& frame : frames) {
        output << frame.frame_id << ',' << frame.bodies.size() << ','
               << frame.stats.detection_ms << ',' << frame.stats.pose_ms << ','
               << frame.stats.tracking_ms << ',' << frame.stats.total_ms << ','
               << frame.stats.submitted_frames << ','
               << frame.stats.processed_frames << ','
               << frame.stats.dropped_frames << '\n';
    }
}

void WriteBodyJson(std::ostream& output, const HV_Body& body, const int indent) {
    const std::string spaces(static_cast<std::size_t>(indent), ' ');
    output << spaces << "{\n";
    output << spaces << "  \"track_id\": " << body.track_id << ",\n";
    output << spaces << "  \"bbox_xywh\": [" << body.bbox_px.x << ", "
           << body.bbox_px.y << ", " << body.bbox_px.width << ", "
           << body.bbox_px.height << "],\n";
    output << spaces << "  \"joints\": [\n";
    for (int joint = 0; joint < HV_JOINT_COUNT; ++joint) {
        const HV_Joint& value = body.joints[joint];
        output << spaces << "    {\"index\": " << joint << ", \"x_px\": "
               << value.x_px << ", \"y_px\": " << value.y_px
               << ", \"confidence\": " << value.confidence
               << ", \"valid\": " << (value.valid != 0 ? "true" : "false")
               << '}';
        output << (joint + 1 == HV_JOINT_COUNT ? "\n" : ",\n");
    }
    output << spaces << "  ]\n" << spaces << '}';
}

void WriteSummary(
    const std::filesystem::path& path,
    const Options& options,
    const std::vector<FrameResult>& frames) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write JSON: " + path.string());
    }
    float detection_sum = 0.0F;
    float pose_sum = 0.0F;
    float tracking_sum = 0.0F;
    float total_sum = 0.0F;
    std::size_t min_bodies = std::numeric_limits<std::size_t>::max();
    std::size_t max_bodies = 0;
    int min_valid_joints = HV_JOINT_COUNT;
    std::set<int> track_ids;
    for (const auto& frame : frames) {
        detection_sum += frame.stats.detection_ms;
        pose_sum += frame.stats.pose_ms;
        tracking_sum += frame.stats.tracking_ms;
        total_sum += frame.stats.total_ms;
        min_bodies = std::min(min_bodies, frame.bodies.size());
        max_bodies = std::max(max_bodies, frame.bodies.size());
        for (const auto& body : frame.bodies) {
            track_ids.insert(body.track_id);
            int valid_joints = 0;
            for (const auto& joint : body.joints) {
                valid_joints += joint.valid != 0 ? 1 : 0;
            }
            min_valid_joints = std::min(min_valid_joints, valid_joints);
        }
    }
    if (min_bodies == std::numeric_limits<std::size_t>::max()) {
        min_bodies = 0;
    }
    if (track_ids.empty()) {
        min_valid_joints = 0;
    }
    const float denominator = static_cast<float>(frames.size());
    output << std::fixed << std::setprecision(6);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"input\": \"" << JsonEscape(options.input.generic_string())
           << "\",\n"
           << "  \"width\": " << options.width << ",\n"
           << "  \"height\": " << options.height << ",\n"
           << "  \"fps\": " << options.fps << ",\n"
           << "  \"max_bodies\": " << options.max_bodies << ",\n"
           << "  \"frames_processed\": " << frames.size() << ",\n"
           << "  \"body_count_min\": " << min_bodies << ",\n"
           << "  \"body_count_max\": " << max_bodies << ",\n"
           << "  \"unique_track_ids\": " << track_ids.size() << ",\n"
           << "  \"minimum_valid_joints_per_body\": " << min_valid_joints
           << ",\n"
           << "  \"average_ms\": {\n"
           << "    \"detection\": " << detection_sum / denominator << ",\n"
           << "    \"pose\": " << pose_sum / denominator << ",\n"
           << "    \"tracking\": " << tracking_sum / denominator << ",\n"
           << "    \"total\": " << total_sum / denominator << "\n"
           << "  },\n"
           << "  \"sample_bodies\": [\n";
    const auto& sample = frames.front().bodies;
    for (std::size_t index = 0; index < sample.size(); ++index) {
        WriteBodyJson(output, sample[index], 4);
        output << (index + 1 == sample.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

int Run(const Options& options) {
    const std::string detector = options.detector_model.u8string();
    const std::string pose = options.pose_model.u8string();
    HV_Config config{};
    config.struct_size = sizeof(HV_Config);
    config.max_bodies = options.max_bodies;
    config.detection_threshold = 0.35F;
    config.pose_threshold = 0.30F;
    config.detection_interval = 1;
    config.enable_tracking = 1;
    config.backend = options.backend;
    config.detector_model_path_utf8 = detector.c_str();
    config.pose_model_path_utf8 = pose.c_str();
    Handle handle;
    if (HV_Create(&config, handle.Out()) != HV_OK) {
        throw std::runtime_error(
            std::string("HV_Create failed: ") + HV_GetLastError(nullptr));
    }

    const std::size_t frame_bytes = static_cast<std::size_t>(options.width) *
                                    static_cast<std::size_t>(options.height) * 3U;
    std::vector<std::uint8_t> pixels(frame_bytes);
    std::vector<FrameResult> results;
    results.reserve(static_cast<std::size_t>(options.frames));
    Pipe decoder(options.input, options.frames);
    for (int frame = 0; frame < options.frames; ++frame) {
        if (!decoder.ReadFrame(pixels)) {
            break;
        }
        results.push_back(SubmitAndWait(handle, options, pixels, frame));
    }
    const int decoder_exit = decoder.Close();
    if (decoder_exit != 0) {
        throw std::runtime_error(
            "ffmpeg decoder exited with code " + std::to_string(decoder_exit));
    }
    if (results.empty()) {
        throw std::runtime_error("input produced no video frames");
    }

    const std::filesystem::path parent = options.output_prefix.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::filesystem::path csv = options.output_prefix;
    csv += ".csv";
    std::filesystem::path json = options.output_prefix;
    json += ".json";
    WriteCsv(csv, results);
    WriteSummary(json, options, results);
    std::cout << "PASS frames=" << results.size()
              << " bodies=" << results.back().bodies.size()
              << " total_ms=" << results.back().stats.total_ms
              << " csv=" << csv << " json=" << json << '\n';
    return 0;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        return Run(ParseOptions(argc, argv));
    } catch (const std::exception& exception) {
        std::cerr << "ERROR: " << exception.what() << '\n';
        return 1;
    }
}
