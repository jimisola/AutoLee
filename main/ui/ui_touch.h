#pragma once

#include <cstdint>
#include "lvgl.h"

// LVGL touch UI, ported from src/ui_touch.cpp. See docs/PLAN.md Phase 3.
// *** Build/boot-verified only - the connected board has no screen/touch
// module attached. Layout, color, and touch responsiveness are unverified;
// see main/display_touch.h's note. ***
void buildUI();

// Screen navigation + UI hooks called from motion.cpp/web_server.cpp.
void go(lv_obj_t *scr);
void showJamScreen();
// Reports the outcome of the jam-recovery home and, on success, returns the
// display to the main screen (only if the jam screen is still active).
void ui_jam_recovery_finished(bool homed);
void setRunButtonState(bool running);
void ui_update_main_warning();
void ui_update_tuning_numbers();
void ui_update_endpoint_edit_values();
void ui_update_speed_val();
void ui_update_profile_screen();
void ui_update_sg_val();
void ui_update_batch_val();
void ui_update_batch_remain();
void ui_update_wifi_label();

// Invalidate the whole active screen so LVGL repaints it. Call once, after boot
// has finished: the initial draw from buildUI() can be lost, leaving the panel
// black with every software indicator healthy. See the implementation.
void ui_force_full_repaint();

// Diagnostic for the "panel dark, firmware healthy" report: logs which screen
// LVGL currently has active, whether it is the one buildUI() loaded, the LVGL
// tick (frozen => the LVGL task has stopped), and the backlight pin's actual
// level. Call from a task that is NOT watchdog-subscribed - it takes the LVGL
// lock, which is precisely what may be held by whatever is stuck.
void ui_log_heartbeat();
