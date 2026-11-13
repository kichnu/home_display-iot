# PC simulator of the panel GUI: the same src/gui/ code and lv_conf.h, LVGL in an SDL2 window,
# mock data driven from the keyboard (sim/mock.cpp). Build: see README.md ("GUI simulator").
cmake_minimum_required(VERSION 3.16)
project(panel_sim C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Same LVGL as the firmware: the copy PlatformIO downloads for lib_deps (lvgl/lvgl@9.2.2)
set(LVGL_DIR ${ROOT}/.pio/libdeps/esp32s3-lcd43b/lvgl CACHE PATH "LVGL source directory")
if(NOT EXISTS ${LVGL_DIR}/lvgl.h)
    message(FATAL_ERROR "LVGL not found in ${LVGL_DIR} — run `pio run` in the project once (downloads lib_deps)")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${ROOT}/include)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE GUI_SIMULATOR)
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2 m)

file(GLOB GUI_SOURCES ${ROOT}/src/gui/*.cpp)
add_executable(panel_sim main.cpp mock.cpp ${GUI_SOURCES} ${ROOT}/src/gui/fonts.S)
# .incbin paths in fonts.S are relative to the project root
set_source_files_properties(${ROOT}/src/gui/fonts.S PROPERTIES COMPILE_OPTIONS "-Wa,-I${ROOT}")
set_source_files_properties(${ROOT}/src/gui/fonts.S PROPERTIES OBJECT_DEPENDS
    "${ROOT}/assets/fonts/NotoSans-Regular.ttf;${ROOT}/assets/fonts/NotoSans-Bold.ttf")
target_include_directories(panel_sim PRIVATE ${ROOT}/src)
target_compile_options(panel_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(panel_sim PRIVATE lvgl)
