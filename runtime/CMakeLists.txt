target_sources(humanvision_internal PRIVATE
    host/plugin_registry.cpp
    host/runtime_host.cpp
    host/model_pack_manager.cpp
    host/profile_manager.cpp
    plugins/legacy/legacy_pipeline.cpp
    plugins/backend/ort/ort_plugin.cpp)
target_include_directories(humanvision_internal PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_include_directories(humanvision_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
