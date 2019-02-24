cmake_minimum_required(VERSION 2.6)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

add_executable(run_tests tests.cpp)
target_link_libraries(run_tests ${GTEST_LIBRARIES} pthread)