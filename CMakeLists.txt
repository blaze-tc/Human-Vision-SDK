cmake_minimum_required(VERSION 3.24)

project(
    HumanVisionSDK
    VERSION 0.3.0
    DESCRIPTION "Human Vision SDK live cameras and recognition regions"
    LANGUAGES C CXX)

include(CTest)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

add_subdirectory(native)
add_subdirectory(runtime)
if(NOT ANDROID)
    add_subdirectory(tools/benchmark)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests/native)
endif()
