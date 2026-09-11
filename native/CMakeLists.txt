find_package(Threads REQUIRED)
option(HV_USE_DIRECTML "Enable GPU selection for AUTO on Windows" OFF)
option(HV_USE_QNN "Enable Android QNN EP in a custom ONNX Runtime build" OFF)
set(HV_QNN_HOME "" CACHE PATH "Authorized QAIRT SDK root used to build ONNX Runtime")
if(HV_USE_QNN)
    if(NOT ANDROID)
        message(FATAL_ERROR "HV_USE_QNN requires Android ARM64")
    endif()
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a" OR NOT EXISTS "${HV_QNN_HOME}/include/QNN/QnnInterface.h")
        message(FATAL_ERROR "HV_USE_QNN requires arm64-v8a and HV_QNN_HOME with include/QNN/QnnInterface.h; see docs/QNN_ANDROID_BUILD.md")
    endif()
endif()
set(HV_DIRECTML_ROOT "" CACHE PATH "Extracted Microsoft.AI.DirectML package")

set(
    HV_ONNXRUNTIME_ROOT
    "${PROJECT_SOURCE_DIR}/third_party/onnxruntime"
    CACHE PATH
    "Path to the extracted ONNX Runtime 1.29.0 Windows x64 package")

set(HV_ONNXRUNTIME_INCLUDE "${HV_ONNXRUNTIME_ROOT}/include")
if(ANDROID)
    if(HV_USE_DIRECTML)
        message(FATAL_ERROR "DirectML is Windows-only")
    endif()
    add_library(hv_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(hv_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${HV_ONNXRUNTIME_ROOT}/lib/libonnxruntime.so"
        INTERFACE_INCLUDE_DIRECTORIES "${HV_ONNXRUNTIME_INCLUDE}")
else()
set(HV_ONNXRUNTIME_LIBRARY "${HV_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
set(HV_ONNXRUNTIME_DLL "${HV_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
set(HV_PRIVATE_ONNXRUNTIME_DLL_NAME "humanvision_onnxruntime.dll")
set(
    HV_PRIVATE_ONNXRUNTIME_DEFINITION
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/onnx/humanvision_onnxruntime.def")
set(
    HV_PRIVATE_ONNXRUNTIME_LIBRARY
    "${CMAKE_CURRENT_BINARY_DIR}/humanvision_onnxruntime.lib")

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

if(NOT MSVC)
    message(FATAL_ERROR
        "The current D0 Windows x64 ONNX Runtime isolation build requires MSVC. "
        "Do not publish another platform until it has a verified private runtime dependency strategy.")
endif()

execute_process(
    COMMAND "${CMAKE_AR}"
        /nologo
        "/def:${HV_PRIVATE_ONNXRUNTIME_DEFINITION}"
        "/out:${HV_PRIVATE_ONNXRUNTIME_LIBRARY}"
        /machine:x64
    RESULT_VARIABLE HV_PRIVATE_ONNXRUNTIME_LIBRARY_RESULT
    OUTPUT_VARIABLE HV_PRIVATE_ONNXRUNTIME_LIBRARY_OUTPUT
    ERROR_VARIABLE HV_PRIVATE_ONNXRUNTIME_LIBRARY_ERROR)

if(NOT HV_PRIVATE_ONNXRUNTIME_LIBRARY_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Failed to create the private ONNX Runtime import library.\n"
        "Output: ${HV_PRIVATE_ONNXRUNTIME_LIBRARY_OUTPUT}\n"
        "Error: ${HV_PRIVATE_ONNXRUNTIME_LIBRARY_ERROR}")
endif()

add_library(hv_onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(hv_onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${HV_PRIVATE_ONNXRUNTIME_LIBRARY}"
    IMPORTED_LOCATION "${HV_ONNXRUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${HV_ONNXRUNTIME_INCLUDE}")
endif()

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
if(HV_USE_QNN)
    target_compile_definitions(humanvision_internal PUBLIC HV_USE_QNN)
endif()

target_compile_features(humanvision_internal PUBLIC cxx_std_17)
if(HV_USE_DIRECTML)
    if(NOT EXISTS "${HV_DIRECTML_ROOT}/include/DirectML.h")
        message(FATAL_ERROR "HV_DIRECTML_ROOT must point to Microsoft.AI.DirectML 1.15.4")
    endif()
    target_compile_definitions(humanvision_internal PUBLIC HV_USE_DIRECTML NOMINMAX)
    target_include_directories(humanvision_internal PRIVATE "${HV_DIRECTML_ROOT}/include")
endif()
target_include_directories(humanvision_internal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(humanvision_internal
    PUBLIC
        hv_onnxruntime
        Threads::Threads)
set_target_properties(humanvision_internal PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

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

if(NOT ANDROID)
add_custom_command(TARGET humanvision POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HV_ONNXRUNTIME_DLL}"
        "$<TARGET_FILE_DIR:humanvision>/${HV_PRIVATE_ONNXRUNTIME_DLL_NAME}"
    COMMAND ${CMAKE_COMMAND} -E rm -f
        "$<TARGET_FILE_DIR:humanvision>/onnxruntime.dll"
        "$<TARGET_FILE_DIR:humanvision>/onnxruntime_providers_shared.dll"
    VERBATIM)
endif()

if(HV_USE_DIRECTML)
    add_custom_command(TARGET humanvision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HV_DIRECTML_ROOT}/bin/x64-win/DirectML.dll"
            "$<TARGET_FILE_DIR:humanvision>/hv_dml.dll"
        VERBATIM)
endif()

option(HV_ENABLE_RTSP "Build native RTSP capture with FFmpeg" OFF)
set(HV_FFMPEG_INCLUDE "" CACHE PATH "FFmpeg 7.1 headers")
set(HV_FFMPEG_LIB_DIR "" CACHE PATH "FFmpeg shared libraries and Windows import libraries")
if(HV_ENABLE_RTSP)
    target_sources(humanvision PRIVATE src/input/rtsp_source.cpp)
    target_include_directories(humanvision PRIVATE "${HV_FFMPEG_INCLUDE}")
    foreach(component avformat avcodec avutil swscale)
        if(ANDROID)
            target_link_libraries(humanvision PRIVATE "${HV_FFMPEG_LIB_DIR}/lib${component}.so")
        else()
            target_link_libraries(humanvision PRIVATE "${HV_FFMPEG_LIB_DIR}/${component}.lib")
        endif()
    endforeach()
endif()
