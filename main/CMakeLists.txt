# Sources are grouped by concern (drivers / motion / net / ui) rather than sitting
# flat. Every group is also on INCLUDE_DIRS, so `#include "motion.h"` keeps working
# regardless of which group the including file lives in - the grouping is for
# humans navigating the tree, not a module boundary the compiler enforces.
idf_component_register(
    SRCS "app_main.cpp" "globals.cpp" "settings_store.cpp"
         "drivers/display_touch.cpp" "drivers/axs5106l_touch.cpp"
         "drivers/tmc5160_hal.cpp" "drivers/tmc5160_ctrl.cpp"
         "drivers/stepper.cpp" "drivers/stepper_motor_encoder.c"
         "motion/motion.cpp" "motion/motion_cmd.cpp" "motion/motion_state.cpp"
         "net/wifi_mgr.cpp" "net/web_server.cpp"
         "ui/ui_touch.cpp"
         "../lib/tmc_api/TMC5160/TMC5160.c" "../lib/tmc_api/helpers/CRC.c" "../lib/tmc_api/helpers/Functions.c"
    INCLUDE_DIRS "." "drivers" "motion" "net" "ui"
                 "../lib/tmc_api/TMC5160" "../lib/tmc_api/helpers"
    REQUIRES autolee_logic nvs_flash esp_lcd driver esp_timer esp_driver_rmt esp_driver_pcnt
             esp_driver_gpio esp_driver_spi esp_driver_i2c esp_system esp_wifi esp_netif
             esp_event nvs_flash dns_server psychic_http esp_https_server esp_http_server
             app_update esp_partition espcoredump esp_hw_support spi_flash heap
)

# ---------------------------------------------------------------------------
#  Build provenance
# ---------------------------------------------------------------------------
# Deliberately NOT a version. The version stays where it has always been -
# esp_app_desc_t, populated by ESP-IDF from `git describe --always --tags
# --dirty` - and nothing here duplicates it. These are the facts that string
# does not carry: which branch, which exact commit, when, and whether the image
# was built on someone's laptop or by CI.
#
# Like PROJECT_VER, these are captured at CMake *configure* time, not per build.
# `idf.py reconfigure` refreshes them; a plain rebuild after committing will
# report the previous commit. That is the same staleness the version string
# already has, so it is one rule to remember rather than two.
function(autolee_git_var out_var)
    execute_process(COMMAND git ${ARGN}
                    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
                    OUTPUT_VARIABLE _v OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET RESULT_VARIABLE _rc)
    # A tarball export or a shallow checkout has no git at all. Report the
    # absence rather than failing the build or, worse, baking in a stale value.
    if(NOT _rc EQUAL 0 OR _v STREQUAL "")
        set(_v "unknown")
    endif()
    set(${out_var} "${_v}" PARENT_SCOPE)
endfunction()

autolee_git_var(AUTOLEE_GIT_BRANCH rev-parse --abbrev-ref HEAD)
autolee_git_var(AUTOLEE_GIT_COMMIT rev-parse HEAD)
autolee_git_var(AUTOLEE_GIT_COMMIT_TIME log -1 --format=%cI)
autolee_git_var(AUTOLEE_GIT_COMMIT_COUNT rev-list --count HEAD)

# "ci" when any of the usual CI markers is set, "local" otherwise. This is the
# single most useful field when a rig misbehaves and nobody can say what is on
# it: a release artifact and something flashed off a laptop look identical in
# every other field.
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
    set(AUTOLEE_BUILD_HOST "ci")
else()
    set(AUTOLEE_BUILD_HOST "local")
endif()

# The web UI is a compiled-in string, so a stale one is invisible: the firmware
# version moves, the page does not, and nothing says which page is in the image.
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/net/index_html.h" AUTOLEE_WEBAPP_SHA256)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AUTOLEE_GIT_BRANCH="${AUTOLEE_GIT_BRANCH}"
    AUTOLEE_GIT_COMMIT="${AUTOLEE_GIT_COMMIT}"
    AUTOLEE_GIT_COMMIT_TIME="${AUTOLEE_GIT_COMMIT_TIME}"
    AUTOLEE_GIT_COMMIT_COUNT="${AUTOLEE_GIT_COMMIT_COUNT}"
    AUTOLEE_BUILD_HOST="${AUTOLEE_BUILD_HOST}"
    AUTOLEE_WEBAPP_SHA256="${AUTOLEE_WEBAPP_SHA256}"
)
