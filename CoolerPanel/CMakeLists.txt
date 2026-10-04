cmake_minimum_required(VERSION 3.20)
project(cooler_panel CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/deps.cmake)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_library(MOSQUITTO_LIB mosquitto REQUIRED)

add_library(cooler_config STATIC config/device_config.cpp)
target_include_directories(cooler_config PUBLIC config)
target_link_libraries(cooler_config PUBLIC ArduinoJson)

file(GLOB_RECURSE SHARED_SRC CONFIGURE_DEPENDS shared/*.cpp)
add_library(cooler_shared STATIC ${SHARED_SRC})
target_include_directories(cooler_shared PUBLIC shared shared/model shared/ui)
# ui.cpp's boot -> setup -> main flow (Task 17) takes a DeviceConfig, so
# shared/ needs config/'s type + include dir; PUBLIC on both links so it
# also reaches cooler_sim transitively below.
target_link_libraries(cooler_shared PUBLIC lvgl ArduinoJson cooler_config)

file(GLOB SIM_SRC CONFIGURE_DEPENDS sim/*.cpp)
add_executable(cooler_sim ${SIM_SRC})
target_include_directories(cooler_sim PRIVATE sim sim/third_party
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(cooler_sim PRIVATE cooler_shared lvgl
    ${SDL2_LIBRARIES} ${FREETYPE_LIBRARIES} ${MOSQUITTO_LIB})

enable_testing()
add_executable(cooler_tests
    tests/test_format.cpp
    tests/test_bounds.cpp
    tests/test_state.cpp
    tests/test_history.cpp
    tests/test_history_msg.cpp
    tests/test_backlight_policy.cpp
    tests/test_chart.cpp
    tests/test_app_wiring.cpp
    tests/test_platform_stub.cpp
    tests/test_publish_stub.cpp
    tests/test_commands.cpp
    tests/test_alarm.cpp
    tests/test_panel_config.cpp
    tests/test_status_text.cpp
    config/tests/test_device_config.cpp)
target_include_directories(cooler_tests PRIVATE tests)
target_link_libraries(cooler_tests PRIVATE cooler_shared cooler_config lvgl doctest::doctest)
target_compile_definitions(cooler_tests PRIVATE FIXture_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME unit COMMAND cooler_tests)
add_test(NAME portability_guard COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/portability_guard.sh)
