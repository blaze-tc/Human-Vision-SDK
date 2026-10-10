set(HV_ANDROID_GPU_PROBE_SOURCES
    gpu/android/ahb_slot_ring.cpp
    gpu/vulkan/vulkan_device_identity.cpp
    gpu/android/ahb_capabilities.cpp
    gpu/android/unity_vulkan_bridge.cpp
    gpu/android/unity_vulkan_plugin.cpp)
option(HV_ANDROID_GPU_GATE "Build development-only Android GPU bridge gate" OFF)
option(HV_ANDROID_TOPDOWN_EVAL_TRACE "Trace ncnn detector stages for local top-down evaluation" OFF)
option(HV_ANDROID_NCNN_EXECUTION_TRACE "Diagnostic copied ncnn layer timestamps in production VkMat route" OFF)
option(HV_ANDROID_NCNN_DISPATCH_EXPERIMENT "Private bounded ncnn dispatch experiment; not a shipping dependency" OFF)
if(HV_ANDROID_NCNN_DISPATCH_EXPERIMENT AND (NOT ANDROID OR NOT HV_ANDROID_TOPDOWN_EVAL_TRACE OR HV_ANDROID_NCNN_EXECUTION_TRACE))
    message(FATAL_ERROR "HV_ANDROID_NCNN_DISPATCH_EXPERIMENT requires Android stage trace and excludes execution timestamps")
endif()
if(HV_ANDROID_NCNN_EXECUTION_TRACE)
    if(NOT ANDROID OR NOT HV_ANDROID_TOPDOWN_EVAL_TRACE)
        message(FATAL_ERROR "HV_ANDROID_NCNN_EXECUTION_TRACE requires Android and stage trace")
    endif()
    target_compile_definitions(humanvision_internal PRIVATE HV_ANDROID_NCNN_EXECUTION_TRACE=1)
endif()
option(HV_ANDROID_R4_PARITY "Evaluation-only bounded GPU parity reductions" OFF)
if(HV_ANDROID_R4_PARITY)
    if(NOT ANDROID)
        message(FATAL_ERROR "HV_ANDROID_R4_PARITY requires Android")
    endif()
    target_compile_definitions(humanvision_internal PRIVATE HV_ANDROID_R4_PARITY=1)
    target_compile_definitions(humanvision PRIVATE HV_ANDROID_R4_PARITY=1)
    target_sources(humanvision_internal PRIVATE gpu/android/gpu_parity_probe.cpp gpu/android/gpu_parity_ncnn.cpp gpu/android/gpu_parity_fixture.cpp)
    target_sources(humanvision PRIVATE gpu/android/gpu_parity_image.cpp)
endif()
if(HV_ANDROID_TOPDOWN_EVAL_TRACE)
    if(NOT ANDROID)
        message(FATAL_ERROR "HV_ANDROID_TOPDOWN_EVAL_TRACE requires Android")
    endif()
    target_compile_definitions(humanvision_internal PRIVATE HV_ANDROID_TOPDOWN_EVAL_TRACE=1)
endif()
if(HV_ANDROID_GPU_GATE AND NOT ANDROID)
    message(FATAL_ERROR "HV_ANDROID_GPU_GATE requires Android")
endif()
if(HV_ANDROID_GPU_GATE)
    target_compile_definitions(humanvision_internal PRIVATE HV_ANDROID_GPU_GATE=1)
    target_compile_definitions(humanvision PRIVATE HV_ANDROID_GPU_GATE=1)
    target_sources(humanvision PRIVATE gpu/android/ncnn_gate.cpp)
    target_include_directories(humanvision PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
endif()

target_sources(humanvision_internal PRIVATE
    composition/session.cpp
    composition/region_assignment.cpp
    host/plugin_registry.cpp
    host/runtime_host.cpp
    host/gpu_runtime_host.cpp
    host/model_pack_manager.cpp
    host/profile_manager.cpp
    host/backend_factory.cpp
    services/body_services.cpp
    services/region_mask.cpp
    plugins/pipeline/rtmo/rtmo_pipeline.cpp
    plugins/pipeline/simcc/simcc_pipeline.cpp
    plugins/pipeline/simcc/gpu_track_crops.cpp
    plugins/pipeline/simcc/detector_cadence.cpp
    plugins/pipeline/simcc/topdown_gpu_pipeline.cpp
    plugins/pipeline/yolo/yolo_decoder.cpp
    plugins/pipeline/yolo/yolo_gpu_pipeline.cpp
    plugins/pipeline/yolo/yolo_tensor_pipeline.cpp
    plugins/legacy/legacy_pipeline.cpp
    plugins/backend/ort/ort_plugin.cpp)
target_sources(humanvision_internal PRIVATE plugins/backend/rknn/rknn_backend.cpp plugins/backend/rknn/rknn_android_vendor.cpp)
# Private experimental Android builds opt in explicitly. Proprietary vendor
# headers/runtime remain outside the repository and public release packages.
option(HV_ENABLE_RKNN "Build private RK3588 neural tensor backend" OFF)
set(HV_RKNN_INCLUDE "${PROJECT_SOURCE_DIR}/out/rknn-toolchain/vendor/include" CACHE PATH "Private pinned RKNN 2.3.2 headers")
if(HV_ENABLE_RKNN)
    if(NOT ANDROID)
        message(FATAL_ERROR "HV_ENABLE_RKNN requires Android; host tests use the injected boundary")
    endif()
    if(NOT EXISTS "${HV_RKNN_INCLUDE}/rknn_api.h")
        message(FATAL_ERROR "Missing private RKNN header; use the pinned toolchain receipt")
    endif()
    file(SHA256 "${HV_RKNN_INCLUDE}/rknn_api.h" hv_rknn_header_sha)
    if(NOT hv_rknn_header_sha STREQUAL "c48e11a6f41b451a5fd1e4ad774ea60252d3d94f78bee9b21ea3d21b21deba9a")
        message(FATAL_ERROR "Private RKNN 2.3.2 header SHA-256 mismatch")
    endif()
    target_include_directories(humanvision_internal PRIVATE "${HV_RKNN_INCLUDE}")
    target_compile_definitions(humanvision_internal PRIVATE HV_ENABLE_RKNN=1)
    target_link_libraries(humanvision_internal PRIVATE dl)
endif()
target_sources(humanvision_internal PRIVATE
    plugins/backend/ncnn/ncnn_vulkan_backend.cpp
    plugins/backend/ncnn/ncnn_dense_output.cpp)
target_sources(humanvision_internal PRIVATE plugins/backend/ncnn/ncnn_prepared_input.cpp)
if(ANDROID)
    target_sources(humanvision_internal PRIVATE
        plugins/backend/ncnn/ncnn_android_session.cpp
        plugins/backend/ncnn/ncnn_preprocess.cpp)
endif()

# Android must expose the probe implementation from libhumanvision.so for the
# production bridge. Host tests link the same sources through the internal
# library. Keep the two placements mutually exclusive so no final target ever
# contains a second copy of these symbols.
if(ANDROID)
    target_sources(humanvision PRIVATE ${HV_ANDROID_GPU_PROBE_SOURCES})
else()
    target_sources(humanvision_internal PRIVATE ${HV_ANDROID_GPU_PROBE_SOURCES})
endif()

target_sources(humanvision PRIVATE composition/runtime_c.cpp composition/android_gpu_c.cpp)
target_include_directories(humanvision_internal PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(humanvision_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(ANDROID)
    # Keep the probe entrypoints linked before B4/B5 start calling them, so API-26
    # ELF closure verification covers the actual Android implementation now.
    target_include_directories(humanvision PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a" OR NOT CMAKE_SYSTEM_VERSION EQUAL 26)
        message(FATAL_ERROR "The pinned Android multi-backend build requires arm64-v8a and API 26")
    endif()
    set(HV_NCNN_ROOT "${PROJECT_SOURCE_DIR}/out/ncnn-20260526/android-arm64-api26/install"
        CACHE PATH "Pinned ncnn static Vulkan SDK prepared by prepare_ncnn_android.ps1")
    set(hv_ncnn_receipt "${HV_NCNN_ROOT}/../build-receipt.json")
    if(NOT EXISTS "${hv_ncnn_receipt}")
        message(FATAL_ERROR "Missing pinned ncnn build. Run pwsh -File tools/setup/prepare_ncnn_android.ps1 -Abi arm64-v8a -ApiLevel 26")
    endif()
    file(READ "${hv_ncnn_receipt}" hv_ncnn_build)
    file(SHA256 "${PROJECT_SOURCE_DIR}/third_party/ncnn/provenance.json" hv_ncnn_pin_hash)
    string(JSON hv_ncnn_built_pin GET "${hv_ncnn_build}" provenance_sha256)
    if(NOT hv_ncnn_built_pin STREQUAL hv_ncnn_pin_hash)
        message(FATAL_ERROR "ncnn provenance changed; rerun tools/setup/prepare_ncnn_android.ps1")
    endif()
    string(JSON hv_ncnn_dispatch ERROR_VARIABLE hv_ncnn_dispatch_error
        GET "${hv_ncnn_build}" dispatch_experiment kind)
    if(HV_ANDROID_NCNN_DISPATCH_EXPERIMENT)
        if(hv_ncnn_dispatch_error OR NOT hv_ncnn_dispatch STREQUAL "hv-ncnn-private-dispatch-experiment-v1")
            message(FATAL_ERROR "Dispatch experiment requires a private copied ncnn receipt")
        endif()
        string(JSON hv_ncnn_dispatch_budget GET "${hv_ncnn_build}" dispatch_experiment pending_dispatch_budget)
        string(JSON hv_ncnn_dispatch_shipping GET "${hv_ncnn_build}" dispatch_experiment shipping_eligible)
        if(NOT hv_ncnn_dispatch_budget EQUAL 262144 OR hv_ncnn_dispatch_shipping)
            message(FATAL_ERROR "Dispatch experiment must be bounded to 256K and ineligible for shipping")
        endif()
    elseif(NOT hv_ncnn_dispatch_error)
        message(FATAL_ERROR "Private dispatch ncnn is prohibited without explicit experiment flag")
    endif()
    string(JSON hv_ncnn_diagnostic ERROR_VARIABLE hv_ncnn_diagnostic_error
        GET "${hv_ncnn_build}" execution_diagnostic kind)
    if(HV_ANDROID_NCNN_EXECUTION_TRACE)
        if(hv_ncnn_diagnostic_error OR NOT hv_ncnn_diagnostic STREQUAL "hv-ncnn-execution-diagnostic-v1")
            message(FATAL_ERROR "Execution trace requires separately receipted diagnostic copied ncnn")
        endif()
    elseif(NOT hv_ncnn_diagnostic_error)
        message(FATAL_ERROR "Diagnostic ncnn is prohibited without explicit execution trace flag")
    endif()
    string(JSON hv_ncnn_library_count LENGTH "${hv_ncnn_build}" libraries)
    if(hv_ncnn_library_count LESS 1)
        message(FATAL_ERROR "ncnn build receipt contains no static libraries")
    endif()
    math(EXPR hv_ncnn_library_last "${hv_ncnn_library_count} - 1")
    foreach(index RANGE ${hv_ncnn_library_last})
        string(JSON library_path GET "${hv_ncnn_build}" libraries ${index} path)
        string(JSON library_hash GET "${hv_ncnn_build}" libraries ${index} sha256)
        file(SHA256 "${HV_NCNN_ROOT}/${library_path}" actual_hash)
        if(NOT actual_hash STREQUAL library_hash)
            message(FATAL_ERROR "Pinned ncnn static library hash mismatch: ${library_path}")
        endif()
    endforeach()
    set(hv_ncnn_package_dir "${HV_NCNN_ROOT}/lib/cmake/ncnn")
    set(hv_glslang_package_dir "${HV_NCNN_ROOT}/lib/cmake/glslang")
    # A cached package directory can override PATHS even with NO_DEFAULT_PATH.
    # Force both packages to the installation whose archives were verified above.
    set(ncnn_DIR "${hv_ncnn_package_dir}" CACHE PATH "Verified ncnn package" FORCE)
    set(glslang_DIR "${hv_glslang_package_dir}" CACHE PATH "Verified glslang package" FORCE)
    unset(ncnn_FOUND)
    unset(ncnn_FOUND CACHE)
    unset(glslang_FOUND)
    unset(glslang_FOUND CACHE)
    find_package(ncnn REQUIRED CONFIG PATHS "${hv_ncnn_package_dir}"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT NCNN_VERSION STREQUAL "20260526" OR NOT NCNN_VULKAN OR NCNN_SHARED_LIB OR NCNN_OPENMP)
        message(FATAL_ERROR "ncnn must be pinned 20260526, static, Vulkan enabled, OpenMP disabled")
    endif()
    file(REAL_PATH "${ncnn_DIR}" hv_resolved_ncnn_dir)
    file(REAL_PATH "${hv_ncnn_package_dir}" hv_expected_ncnn_dir)
    if(NOT hv_resolved_ncnn_dir STREQUAL hv_expected_ncnn_dir)
        message(FATAL_ERROR "ncnn package resolved outside verified HV_NCNN_ROOT: ${hv_resolved_ncnn_dir}")
    endif()

    function(hv_verify_ncnn_imported_library target_name)
        if(NOT TARGET "${target_name}")
            message(FATAL_ERROR "Verified ncnn package is missing imported target: ${target_name}")
        endif()
        get_property(generator_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(generator_is_multi_config)
            set(consumer_configs ${CMAKE_CONFIGURATION_TYPES})
        elseif(CMAKE_BUILD_TYPE)
            set(consumer_configs ${CMAKE_BUILD_TYPE})
        else()
            set(consumer_configs __HV_CONFIGURATION_LESS__)
        endif()
        foreach(consumer_config IN LISTS consumer_configs)
            if(consumer_config STREQUAL "__HV_CONFIGURATION_LESS__")
                set(location_property LOCATION)
                set(config_label "configuration-less")
            else()
                string(TOUPPER "${consumer_config}" consumer_config_upper)
                set(location_property "LOCATION_${consumer_config_upper}")
                set(config_label "${consumer_config}")
            endif()
            # LOCATION_<CONFIG> asks CMake for the archive it will actually link,
            # including MAP_IMPORTED_CONFIG_<CONFIG> and generic/fallback rules.
            get_target_property(imported_location "${target_name}" "${location_property}")
            if(NOT imported_location OR imported_location MATCHES "-NOTFOUND$")
                message(FATAL_ERROR "Verified ncnn imported target has no selected library for ${config_label}: ${target_name}")
            endif()
            file(REAL_PATH "${imported_location}" imported_real)
            set(receipt_match FALSE)
            foreach(receipt_index RANGE ${hv_ncnn_library_last})
                string(JSON receipt_path GET "${hv_ncnn_build}" libraries ${receipt_index} path)
                string(JSON receipt_hash GET "${hv_ncnn_build}" libraries ${receipt_index} sha256)
                file(REAL_PATH "${HV_NCNN_ROOT}/${receipt_path}" receipt_real)
                if(imported_real STREQUAL receipt_real)
                    file(SHA256 "${imported_real}" imported_hash)
                    if(NOT imported_hash STREQUAL receipt_hash)
                        message(FATAL_ERROR "Resolved ncnn imported library hash mismatch for ${config_label}: ${target_name} -> ${imported_real}")
                    endif()
                    set(receipt_match TRUE)
                    break()
                endif()
            endforeach()
            if(NOT receipt_match)
                message(FATAL_ERROR "Resolved ncnn imported library is not listed in the verified ncnn receipt for ${config_label}: ${target_name} -> ${imported_real}")
            endif()
        endforeach()
    endfunction()
    foreach(imported_target IN ITEMS ncnn glslang::SPIRV glslang::OSDependent
            glslang::glslang glslang::MachineIndependent glslang::GenericCodeGen
            glslang::glslang-default-resource-limits)
        hv_verify_ncnn_imported_library("${imported_target}")
    endforeach()
    target_include_directories(humanvision PRIVATE "${PROJECT_SOURCE_DIR}/third_party/unity-plugin-api/include")
    target_link_libraries(humanvision_internal PRIVATE ncnn)
    # All modes share this binary; retain the static backend even before B5
    # registration exists. Keep the existing ORT/FFmpeg linkage intact.
    target_link_libraries(humanvision PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ncnn>"
        "-Wl,--no-as-needed" android vulkan log "-Wl,--as-needed")
    set_property(TARGET humanvision PROPERTY LINK_LIBRARY_OVERRIDE_ncnn WHOLE_ARCHIVE)
endif()
