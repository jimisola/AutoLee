# Extra build rules for the motion-sequencing suite (docs/PLAN.md Phase 8):
# unlike the other suites, this one compiles the REAL firmware sources
# main/motion/motion.cpp + motion_state.cpp against the host fakes in
# host_test/fakes (fake stepper::/tmc5160:: bodies behind the real driver
# headers, plus minimal ESP-IDF/LVGL header stubs). Included by
# host_test/CMakeLists.txt; `suite` and `TEST_DIR` come from there.
target_sources(${suite} PRIVATE
  "${TEST_DIR}/fakes/fake_hw.cpp"
  "${TEST_DIR}/../main/motion/motion.cpp"
  "${TEST_DIR}/../main/motion/motion_state.cpp")

target_include_directories(${suite} PRIVATE "${TEST_DIR}/fakes")

# SYSTEM for the firmware headers and the ESP-IDF/LVGL stubs: main/config.h
# declares `static const char *` constants that are unused in any single
# translation unit, which -Wall -Wextra -Werror rejects. Marking them system
# includes keeps the strict flags fully in force for the sources actually
# compiled here (motion.cpp, motion_state.cpp, the fakes, the test) without
# having to relax them project-wide or edit config.h for the tests' benefit.
target_include_directories(${suite} SYSTEM PRIVATE
  "${TEST_DIR}/fakes/include"  # shadows the real ESP-IDF/LVGL headers
  "${TEST_DIR}/../main"
  "${TEST_DIR}/../main/drivers"
  "${TEST_DIR}/../main/motion"
  "${TEST_DIR}/../main/ui")
