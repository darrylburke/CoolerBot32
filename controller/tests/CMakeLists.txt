# Host tests for cooler_logic.h. From the controller/ directory:
#   cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(cooler_controller_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(controller_tests ${TEST_SRC})
target_include_directories(controller_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(controller_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(controller_tests PRIVATE doctest::doctest)
add_test(NAME controller COMMAND controller_tests)
