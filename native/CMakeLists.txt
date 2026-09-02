find_package(Threads REQUIRED)

set(
    HV_ONNXRUNTIME_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/onnxruntime"
    CACHE PATH
    "Path to the extracted ONNX Runtime 1.29.0 Windows x64 package")

set(HV_ONNXRUNTIME_INCLUDE "${HV_ONNXRUNTIME_ROOT}/include")
set(HV_ONNXRUNTIME_LIBRARY "${HV_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
set(HV_ONNXRUNTIME_DLL "${HV_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
set(
    HV_ONNXRUNTIME_PROVIDERS_SHARED_DLL
    "${HV_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")

foreach(required_path
        IN ITEMS
            "${HV_ONNXRUNTIME_INCLUDE}/onnxruntime_cxx_api.h"
            "${HV_ONNXRUNTIME_LIBRARY}"
            "${HV_ONNXRUNTIME_DLL}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "ONNX Runtime 1.29.0 CPU x64 is missing: ${required_path}\n"
            "Run: powershell -ExecutionPolicy Bypass -File tools/setup/download_onnxruntime.ps1")
    endif()
endforeach()

add_library(hv_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(hv_onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${HV_ONNXRUNTIME_LIBRARY}"
    IMPORTED_LOCATION "${HV_ONNXRUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${HV_ONNXRUNTIME_INCLUDE}")

add_library(humanvision_internal STATIC
    src/backend/onnx/onnx_runtime_backend.cpp
    src/core/frame_buffer.cpp
    src/core/humanvision_engine.cpp
    src/core/latest_frame_slot.cpp
    src/core/result_snapshot_store.cpp
    src/core/stats_collector.cpp
    src/models/rtmdet/rtmdet_model.cpp
    src/models/rtmpose/pose_affine.cpp
    src/models/rtmpose/rtmpose_preprocess.cpp
    src/models/rtmpose/rtmpose_model.cpp
    src/models/rtmpose/simcc_decoder.cpp
    src/tracking/center_iou_tracker.cpp)

add_library(HumanVision::Internal ALIAS humanvision_internal)

target_compile_features(humanvision_internal PUBLIC cxx_std_17)
target_include_directories(humanvision_internal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(humanvision_internal
    PUBLIC
        hv_onnxruntime
        Threads::Threads)
set_target_properties(humanvision_internal PROPERTIES CXX_EXTENSIONS OFF)

add_library(humanvision SHARED
    src/core/humanvision_c.cpp
    src/core/version.cpp)

add_library(HumanVision::Core ALIAS humanvision)

target_compile_features(humanvision PUBLIC cxx_std_17)
target_compile_definitions(humanvision
    PRIVATE
        HUMANVISION_BUILDING_DLL
        HUMANVISION_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(humanvision
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)
target_link_libraries(humanvision PRIVATE HumanVision::Internal)

set_target_properties(humanvision PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME humanvision)

if(MSVC)
    target_compile_options(humanvision_internal PRIVATE /W4 /permissive-)
    target_compile_options(humanvision PRIVATE /W4 /permissive-)
endif()

add_custom_command(TARGET humanvision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HV_ONNXRUNTIME_DLL}"
        "$<TARGET_FILE_DIR:humanvision>"
    VERBATIM)

if(EXISTS "${HV_ONNXRUNTIME_PROVIDERS_SHARED_DLL}")
    add_custom_command(TARGET humanvision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HV_ONNXRUNTIME_PROVIDERS_SHARED_DLL}"
            "$<TARGET_FILE_DIR:humanvision>"
        VERBATIM)
endif()
