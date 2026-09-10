target_sources(humanvision_internal PRIVATE
    host/plugin_registry.cpp
    host/runtime_host.cpp)
target_include_directories(humanvision_internal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
