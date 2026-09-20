set(HV_ANDROID_GPU_PROBE_SOURCES
    gpu/vulkan/vulkan_device_identity.cpp
    gpu/android/ahb_capabilities.cpp)

target_sources(humanvision_internal PRIVATE
    composition/session.cpp
    host/plugin_registry.cpp
    host/runtime_host.cpp
    host/model_pack_manager.cpp
    host/profile_manager.cpp
    host/backend_factory.cpp
    services/body_services.cpp
    services/region_mask.cpp
    plugins/pipeline/rtmo/rtmo_pipeline.cpp
    plugins/pipeline/simcc/simcc_pipeline.cpp
    plugins/legacy/legacy_pipeline.cpp
    plugins/backend/ort/ort_plugin.cpp)

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
