/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ui_display ui_display;
ui_display *ui_display_open(const char *device);
void ui_display_close(ui_display *display);
/* One UI-thread producer. Pixels begin at the rectangle with an explicit stride.
 * The last rectangle queues a complete frame; older pending frames may coalesce.
 * Close drains the final frame and joins the display worker before releasing it. */
int ui_display_update(ui_display *display, int x1, int y1, int x2, int y2,
                      const uint8_t *pixels, unsigned stride, int last);
#ifdef __cplusplus
}
#endif
#endif
