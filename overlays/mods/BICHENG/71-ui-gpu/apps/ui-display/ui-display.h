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
/* Pixels begin at the supplied rectangle, with an explicit source stride. */
int ui_display_update(ui_display *display, int x1, int y1, int x2, int y2,
                      const uint8_t *pixels, unsigned stride, int last);
#ifdef __cplusplus
}
#endif
#endif
