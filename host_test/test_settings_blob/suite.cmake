# Extra build rules for the persisted-settings blob/migration suite.
# Unlike the lib/autolee_logic suites, this one includes a firmware header
# (main/settings_blob.h, which pulls in main/config.h for NUM_PROFILES and the
# validation bounds). No sources to add: settings_blob.h is header-only and
# ESP-IDF-free by design - the NVS half lives in main/settings_store.cpp and is
# deliberately not compiled here. Included by host_test/CMakeLists.txt.
#
# SYSTEM, for the same reason test_motion_seq does it: main/config.h declares
# `static const char *` constants that are unused in any single translation
# unit, which -Wall -Wextra -Werror would otherwise reject.
target_include_directories(${suite} SYSTEM PRIVATE "${TEST_DIR}/../main")
