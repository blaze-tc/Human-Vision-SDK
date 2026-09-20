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

target_sources(humanvision PRIVATE composition/runtime_c.cpp composition/android_gpu_c.cpp)
target_include_directories(humanvision_internal PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(humanvision_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(ANDROID)
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
    list(APPEND CMAKE_FIND_ROOT_PATH "${HV_NCNN_ROOT}")
    find_package(ncnn REQUIRED CONFIG PATHS "${HV_NCNN_ROOT}/lib/cmake/ncnn"
        NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT NCNN_VERSION STREQUAL "20260526" OR NOT NCNN_VULKAN OR NCNN_SHARED_LIB OR NCNN_OPENMP)
        message(FATAL_ERROR "ncnn must be pinned 20260526, static, Vulkan enabled, OpenMP disabled")
    endif()
    target_include_directories(humanvision PRIVATE "${PROJECT_SOURCE_DIR}/third_party/unity-plugin-api/include")
    target_link_libraries(humanvision_internal PRIVATE ncnn)
    # All modes share this binary; retain the static backend even before B5
    # registration exists. Keep the existing ORT/FFmpeg linkage intact.
    target_link_libraries(humanvision PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ncnn>"
        "-Wl,--no-as-needed" android vulkan log "-Wl,--as-needed")
    set_property(TARGET humanvision PROPERTY LINK_LIBRARY_OVERRIDE_ncnn WHOLE_ARCHIVE)
endif()
