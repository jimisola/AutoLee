cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/lib")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# lv_conf.h must be visible to every component that includes lvgl.h, so it's
# set project-wide here.
idf_build_set_property(COMPILE_DEFINITIONS "-DLV_CONF_INCLUDE_SIMPLE" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}/include" APPEND)

project(autolee)
