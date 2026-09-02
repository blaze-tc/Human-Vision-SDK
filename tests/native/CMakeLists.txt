include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)

add_executable(humanvision_smoke_tests
    test_smoke.cpp)

target_link_libraries(humanvision_smoke_tests
    PRIVATE
        HumanVision::Core
        GTest::gtest_main)

target_compile_features(humanvision_smoke_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(humanvision_smoke_tests PRIVATE /W4 /permissive-)
endif()

include(GoogleTest)
gtest_discover_tests(humanvision_smoke_tests)

add_executable(humanvision_native_tests
    test_c_api.cpp
    test_latest_frame_slot.cpp
    test_onnx_runtime_backend.cpp
    test_result_snapshot_store.cpp
    test_rtmdet_model.cpp)

target_link_libraries(humanvision_native_tests
    PRIVATE
        HumanVision::Core
        HumanVision::Internal
        GTest::gtest_main)

target_include_directories(humanvision_native_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/native/src
        ${PROJECT_SOURCE_DIR}/tests/testdata)

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/models/detector/rtmdet_tiny_640.onnx" HV_TEST_DETECTOR_MODEL_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/add_one.onnx" HV_TEST_BACKEND_MODEL_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/d0_1_human_pose.bgr" HV_TEST_RAW_IMAGE_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/d0_2_two_people.bgr" HV_TEST_MULTI_RAW_IMAGE_PATH)
target_compile_definitions(humanvision_native_tests
    PRIVATE
        HV_TEST_DETECTOR_MODEL_PATH="${HV_TEST_DETECTOR_MODEL_PATH}"
        HV_TEST_BACKEND_MODEL_PATH="${HV_TEST_BACKEND_MODEL_PATH}"
        HV_TEST_RAW_IMAGE_PATH="${HV_TEST_RAW_IMAGE_PATH}"
        HV_TEST_MULTI_RAW_IMAGE_PATH="${HV_TEST_MULTI_RAW_IMAGE_PATH}")

target_compile_features(humanvision_native_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(humanvision_native_tests PRIVATE /W4 /permissive-)
endif()

gtest_discover_tests(humanvision_native_tests)
