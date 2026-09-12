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

target_sources(humanvision PRIVATE composition/runtime_c.cpp)
target_include_directories(humanvision_internal PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(humanvision_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
