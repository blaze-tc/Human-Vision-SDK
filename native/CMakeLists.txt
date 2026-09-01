add_library(humanvision SHARED
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

set_target_properties(humanvision PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME humanvision)

if(MSVC)
    target_compile_options(humanvision PRIVATE /W4 /permissive-)
endif()
