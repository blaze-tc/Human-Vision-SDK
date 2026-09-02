#pragma once

#include "humanvision/humanvision_c.h"
#include "d0_2_fixture_contract.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef HV_TEST_DETECTOR_MODEL_PATH
#error HV_TEST_DETECTOR_MODEL_PATH must be defined by CMake
#endif

#ifndef HV_TEST_BACKEND_MODEL_PATH
#error HV_TEST_BACKEND_MODEL_PATH must be defined by CMake
#endif

#ifndef HV_TEST_RAW_IMAGE_PATH
#error HV_TEST_RAW_IMAGE_PATH must be defined by CMake
#endif

#ifndef HV_TEST_MULTI_RAW_IMAGE_PATH
#error HV_TEST_MULTI_RAW_IMAGE_PATH must be defined by CMake
#endif

namespace humanvision::test {

inline std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open test file: " + path.string());
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

inline HV_Config MakeConfig(int max_bodies = 4) {
    HV_Config config{};
    config.struct_size = sizeof(HV_Config);
    config.max_bodies = max_bodies;
    config.detection_threshold = 0.35F;
    config.pose_threshold = 0.30F;
    config.detection_interval = 1;
    config.enable_tracking = 0;
    config.backend = HV_BACKEND_ONNX_CPU;
    config.detector_model_path_utf8 = HV_TEST_DETECTOR_MODEL_PATH;
    config.pose_model_path_utf8 = nullptr;
    return config;
}

inline HV_VideoFrame MakeBgrFrame(
    const std::vector<std::uint8_t>& bytes,
    int width,
    int height,
    std::int64_t frame_id,
    std::int64_t timestamp_us = 0) {
    HV_VideoFrame frame{};
    frame.struct_size = sizeof(HV_VideoFrame);
    frame.width = width;
    frame.height = height;
    frame.stride_bytes = width * 3;
    frame.pixel_format = HV_PIXEL_BGR24;
    frame.frame_id = frame_id;
    frame.timestamp_us = timestamp_us;
    frame.data = bytes.data();
    frame.data_bytes = static_cast<std::int32_t>(bytes.size());
    return frame;
}

}  // namespace humanvision::test
