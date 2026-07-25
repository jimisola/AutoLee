# Host (Linux) tests for lib/autolee_logic, built on ESP-IDF's
# toolchain-independent Unity. See docs/PLAN.md Phase 2.
#
# Plain CMake + CTest, not an idf.py app: each test_*/test_*.cpp defines its own
# main(), so the suites can't be linked into one idf.py "linux" target app.
# Requires IDF_PATH (i.e. ESP-IDF's export.sh sourced) only to locate the
# vendored Unity source.
#
# Named per ESP-IDF convention: `host_test/` is what ESP-IDF itself uses for
# tests that run on the build machine (9 components in v5.3.2, e.g. esp_event,
# nvs_flash, log). Deliberately NOT `test_apps/` - that convention (77
# components) means an on-target application you flash and run on the chip,
# which these are not. If genuine on-target tests are added later, `test_apps/`
# is free for them.
cmake_minimum_required(VERSION 3.16)
project(autolee_logic_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AUTOLEE_COVERAGE "Instrument test suites for gcov/gcovr coverage" OFF)

if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set - source $IDF_PATH/export.sh first")
endif()

set(UNITY_DIR "$ENV{IDF_PATH}/components/unity/unity/src")

add_library(unity STATIC "${UNITY_DIR}/unity.c")
target_include_directories(unity PUBLIC "${UNITY_DIR}")

set(AUTOLEE_LOGIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/autolee_logic")
set(TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()

file(GLOB test_suites RELATIVE "${TEST_DIR}" "${TEST_DIR}/test_*")
foreach(suite ${test_suites})
  set(suite_dir "${TEST_DIR}/${suite}")
  if(IS_DIRECTORY "${suite_dir}")
    file(GLOB suite_src "${suite_dir}/*.cpp")
    add_executable(${suite} ${suite_src})
    target_include_directories(${suite} PRIVATE "${AUTOLEE_LOGIC_DIR}")
    target_link_libraries(${suite} PRIVATE unity)
    target_compile_options(${suite} PRIVATE -Wall -Wextra -Werror)
    # Absolute path to the shared JSON contract example, so test_state_json can
    # assert the serializer stays byte-for-byte in sync with the contract file
    # (api/schemas/state.example.json, which CI validates against the schema).
    # Harmless for the suites that don't use it.
    target_compile_definitions(${suite} PRIVATE
      STATE_EXAMPLE_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../api/schemas/state.example.json")
    # Per-suite extension point: a suite needing extra sources or include dirs
    # (test_motion_seq compiles main/motion/motion.cpp against host_test/fakes)
    # drops a suite.cmake next to its test file. `suite` and `TEST_DIR` are in
    # scope there.
    if(EXISTS "${suite_dir}/suite.cmake")
      include("${suite_dir}/suite.cmake")
    endif()
    if(AUTOLEE_COVERAGE)
      target_compile_options(${suite} PRIVATE --coverage -O0 -g)
      target_link_options(${suite} PRIVATE --coverage)
    endif()
    add_test(NAME ${suite} COMMAND ${suite})
  endif()
endforeach()
