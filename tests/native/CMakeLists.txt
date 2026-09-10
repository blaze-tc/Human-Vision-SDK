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
    test_pose_affine.cpp
    test_result_snapshot_store.cpp
    test_rtmdet_model.cpp
    test_rtmpose_model.cpp
    test_simcc_decoder.cpp
    test_stats_collector.cpp
    test_tracker.cpp)

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
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/models/pose/rtmpose_s_256x192.onnx" HV_TEST_POSE_MODEL_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/add_one.onnx" HV_TEST_BACKEND_MODEL_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/d0_1_human_pose.bgr" HV_TEST_RAW_IMAGE_PATH)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/testdata/d0_2_two_people.bgr" HV_TEST_MULTI_RAW_IMAGE_PATH)
target_compile_definitions(humanvision_native_tests
    PRIVATE
        HV_TEST_DETECTOR_MODEL_PATH="${HV_TEST_DETECTOR_MODEL_PATH}"
        HV_TEST_POSE_MODEL_PATH="${HV_TEST_POSE_MODEL_PATH}"
        HV_TEST_BACKEND_MODEL_PATH="${HV_TEST_BACKEND_MODEL_PATH}"
        HV_TEST_RAW_IMAGE_PATH="${HV_TEST_RAW_IMAGE_PATH}"
        HV_TEST_MULTI_RAW_IMAGE_PATH="${HV_TEST_MULTI_RAW_IMAGE_PATH}")

target_compile_features(humanvision_native_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(humanvision_native_tests PRIVATE /W4 /permissive-)
endif()

gtest_discover_tests(humanvision_native_tests)

add_executable(humanvision_api_contract_tests
    test_api_contract.cpp)

target_link_libraries(humanvision_api_contract_tests
    PRIVATE
        HumanVision::Core
        GTest::gtest_main)

target_compile_features(humanvision_api_contract_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(humanvision_api_contract_tests PRIVATE /W4 /permissive-)
endif()

add_test(NAME api_contract COMMAND humanvision_api_contract_tests)

if(MSVC)
    get_filename_component(HV_MSVC_BIN_DIRECTORY "${CMAKE_LINKER}" DIRECTORY)
    set(HV_DUMPBIN_PATH "${HV_MSVC_BIN_DIRECTORY}/dumpbin.exe")

    add_test(
        NAME humanvision_native_dependency_isolation
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${PROJECT_SOURCE_DIR}/tools/test/test_native_dependency_isolation.ps1"
            -DumpbinPath "${HV_DUMPBIN_PATH}"
            -HumanVisionDll "$<TARGET_FILE:humanvision>"
            -PrivateRuntimeDll "$<TARGET_FILE_DIR:humanvision>/humanvision_onnxruntime.dll")
endif()

add_executable(humanvision_plugin_tests ../runtime/test_plugin_host.cpp)
target_link_libraries(humanvision_plugin_tests PRIVATE HumanVision::Internal GTest::gtest_main)
target_include_directories(humanvision_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/runtime ${PROJECT_SOURCE_DIR}/runtime/include)
gtest_discover_tests(humanvision_plugin_tests)

add_executable(humanvision_plugin_c_contract ../runtime/plugin_c_contract.c)
target_include_directories(humanvision_plugin_c_contract PRIVATE ${PROJECT_SOURCE_DIR}/native/include ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_features(humanvision_plugin_c_contract PRIVATE c_std_11)
add_test(NAME plugin_c_abi COMMAND humanvision_plugin_c_contract)
if(WIN32)
    add_library(humanvision_fixture_plugin SHARED ../runtime/fixture_plugin.c)
    target_compile_definitions(humanvision_fixture_plugin PRIVATE HV_PLUGIN_BUILD_DLL)
    target_include_directories(humanvision_fixture_plugin PRIVATE ${PROJECT_SOURCE_DIR}/native/include ${PROJECT_SOURCE_DIR}/runtime/include)
    target_compile_definitions(humanvision_plugin_tests PRIVATE HV_TEST_PLUGIN_PATH="$<TARGET_FILE:humanvision_fixture_plugin>")
    add_dependencies(humanvision_plugin_tests humanvision_fixture_plugin)
endif()
