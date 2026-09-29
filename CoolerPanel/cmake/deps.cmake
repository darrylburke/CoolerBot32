include(FetchContent)

# lv_conf.h turns on LV_USE_FREETYPE, so LVGL's own sources (src/libs/freetype/*.c)
# need ft2build.h. LVGL's env_support/cmake/os_desktop.cmake does not look up or
# link Freetype for its own `lvgl` target -- it only builds the sources assuming
# the includes are already reachable. find_package + link it into the `lvgl`
# target below (in addition to the top-level CMakeLists.txt's own find_package(Freetype)
# for llmmon_sim), otherwise lv_freetype.c fails with "ft2build.h: No such file or directory".
find_package(Freetype REQUIRED)

set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/../shared/lv_conf.h CACHE FILEPATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.3.0)

# LVGL's env_support/cmake/os_desktop.cmake globs *all* src/*.S files unconditionally,
# which pulls in ARM-only NEON/Helium blend assembly that fails to assemble on
# non-ARM hosts (our lv_conf.h's `#include <stdint.h>` gets pasted into the .S
# preprocessing and the assembler chokes on the resulting C typedefs). Source file
# properties are directory-scoped in CMake, so setting HEADER_FILE_ONLY from this
# top-level file has no effect on the target created inside lvgl's own subdirectory
# scope -- instead, populate the source first and delete the two ARM-only files
# from the tree before add_subdirectory() runs its GLOB_RECURSE, when not building
# for an ARM host.
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    FetchContent_Populate(lvgl)
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
        file(REMOVE
            ${lvgl_SOURCE_DIR}/src/draw/sw/blend/neon/lv_blend_neon.S
            ${lvgl_SOURCE_DIR}/src/draw/sw/blend/helium/lv_blend_helium.S)
    endif()
    add_subdirectory(${lvgl_SOURCE_DIR} ${lvgl_BINARY_DIR})
    target_include_directories(lvgl PUBLIC ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${FREETYPE_LIBRARIES})
endif()

FetchContent_Declare(ArduinoJson GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git GIT_TAG v7.4.3)
FetchContent_MakeAvailable(ArduinoJson)

FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.4.11)
FetchContent_MakeAvailable(doctest)
