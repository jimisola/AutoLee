// Host stub for lvgl.h. main/globals.h and main/ui/ui_touch.h declare LVGL
// object pointers; motion.cpp never dereferences one, so an incomplete type is
// all the host build needs.
#pragma once

typedef struct _lv_obj_t lv_obj_t;
