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
