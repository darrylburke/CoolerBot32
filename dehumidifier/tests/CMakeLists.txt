# Host tests for dehum_logic.h. From the dehumidifier/ directory:
#   cmake -S tests -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(dehum_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)

enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(dehum_tests ${TEST_SRC})
target_include_directories(dehum_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(dehum_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(dehum_tests PRIVATE doctest::doctest)
add_test(NAME dehum COMMAND dehum_tests)
