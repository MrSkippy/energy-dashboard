# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set( CMAKE_C_FLAGS "-Wno-unused -DLV_CONF_INCLUDE_SIMPLE=1" )
set( CMAKE_CXX_FLAGS "-Wno-unused -DLV_CONF_INCLUDE_SIMPLE=1" )

set(IDF_TARGET esp32s3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS components)

include_directories( "main" ) #to find lv_conf.h in 'main' with LV_CONF_INCLUDE_SIMPLE

project(Energy_Dashboard)

# LVGL's own log calls (e.g. lv_svg_decoder.c) use %u for uint32_t, which is
# 'long unsigned int' on Xtensa -> -Werror=format fails once LV_USE_LOG is on.
# Downgrade that to a warning for the LVGL component only.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-error=format)
