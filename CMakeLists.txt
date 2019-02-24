cmake_minimum_required(VERSION 2.6)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Suppressing benchmark's tests" FORCE)
add_subdirectory(external/benchmark)
add_subdirectory(external/googletest)

add_executable(run_test tests.cpp)

# target_link_libraries(run_bench benchmark)
target_link_libraries(run_test gtest)