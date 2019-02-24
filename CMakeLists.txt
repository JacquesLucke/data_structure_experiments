cmake_minimum_required(VERSION 2.6)

set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)
add_subdirectory(external/benchmark)
add_subdirectory(external/googletest)

add_executable(run_test tests.cpp)
add_executable(run_benchmarks benchmarks.cpp)

target_link_libraries(run_test gtest)
target_link_libraries(run_benchmarks benchmark)