#ifndef NATIVE_PAGE_CAPTURE_H
#define NATIVE_PAGE_CAPTURE_H
#include <stdint.h>

/* Call only from the original GUI main-loop tick, after releasing the LVGL lock.
 * This function never invalidates a screen, dispatches a task or sends input. */
void stock_draw_tick(uintptr_t vendor_base, void *display);
#endif
