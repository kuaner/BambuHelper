cmake_minimum_required(VERSION 3.16)
project(p4_preview LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LGFX "${ROOT}/.pio/libdeps/ws_p4_lcd_7b_rev1/LovyanGFX/src" CACHE PATH "LovyanGFX source")
find_package(SDL2 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/embed_font.py
  ${ROOT} ${CMAKE_CURRENT_BINARY_DIR}/p4_sans_preview.h COMMAND_ERROR_IS_FATAL ANY)
file(GLOB LGFX_SOURCES
  "${LGFX}/lgfx/Fonts/efont/*.c" "${LGFX}/lgfx/Fonts/IPA/*.c"
  "${LGFX}/lgfx/Fonts/lvgl/*.c" "${LGFX}/lgfx/utility/*.c"
  "${LGFX}/lgfx/v1/*.cpp" "${LGFX}/lgfx/v1/lv_font/*.c"
  "${LGFX}/lgfx/v1/misc/*.cpp" "${LGFX}/lgfx/v1/platforms/sdl/*.cpp")
add_executable(p4_preview preview.cpp ${ROOT}/src/display_p4.cpp ${ROOT}/src/fonts.cpp
  ${ROOT}/src/hms_lookup.cpp ${LGFX_SOURCES}
  ${LGFX}/lgfx/v1/panel/Panel_Device.cpp ${LGFX}/lgfx/v1/panel/Panel_FrameBufferBase.cpp)
target_compile_definitions(p4_preview PRIVATE P4_PREVIEW=1 LGFX_SDL BOARD_IS_WS_P4_7B=1 DISPLAY_1024x600=1 BOARD_HAS_PSRAM=1 BACKLIGHT_PIN=32)
target_include_directories(p4_preview PRIVATE ${CMAKE_CURRENT_BINARY_DIR} stubs ${ROOT}/src ${ROOT}/include ${LGFX})
target_link_libraries(p4_preview PRIVATE SDL2::SDL2)
