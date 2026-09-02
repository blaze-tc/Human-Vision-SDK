add_executable(hv_video_benchmark
    hv_video_benchmark.cpp)

target_compile_features(hv_video_benchmark PRIVATE cxx_std_17)
target_link_libraries(hv_video_benchmark PRIVATE HumanVision::Core)
set_target_properties(hv_video_benchmark PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(hv_video_benchmark PRIVATE /W4 /permissive-)
endif()
