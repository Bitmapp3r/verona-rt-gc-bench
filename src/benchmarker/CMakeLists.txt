cmake_minimum_required(VERSION 3.16)

project(benchmarker CXX)

add_executable(benchmarker
    benchmark_main.cc
)

# Include directories
target_include_directories(benchmarker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/rt
    ${CMAKE_SOURCE_DIR}/test
)

# Link to existing rt interface library
target_link_libraries(benchmarker
    verona_rt
)

set_target_properties(benchmarker PROPERTIES
    CXX_STANDARD 17
)
