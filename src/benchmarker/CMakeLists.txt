add_executable(benchmarker
    benchmark_main.cc
)

target_include_directories(benchmarker 
    PRIVATE ${CMAKE_SOURCE_DIR}/test
)

target_link_libraries(benchmarker
    PRIVATE verona_rt
)

target_compile_features(benchmarker PRIVATE cxx_std_17)