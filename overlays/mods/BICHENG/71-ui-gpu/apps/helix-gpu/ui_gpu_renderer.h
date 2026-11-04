// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UI_GPU_RENDERER_H
#define UI_GPU_RENDERER_H
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Main thread, after the display's normal output callback has been installed. */
bool ui_gpu_renderer_attach(lv_display_t *display);
#ifdef __cplusplus
}
#endif
#endif
