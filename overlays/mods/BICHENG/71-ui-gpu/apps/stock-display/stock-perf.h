#ifndef STOCK_PERF_H
#define STOCK_PERF_H

#include <stdint.h>

/* Call on the UI thread under lv_lock, after verifying the vendor binary hash.
 * Returns 1 when attached, 0 for unsupported versions, or -1 for invalid arguments.
 * Repeated calls update configuration without adding another callback. */
int stock_perf_attach(uintptr_t base, const char *verified_version, void *display,
                      int gpu_enabled, unsigned refresh_period_ms, unsigned animation_period_ms);

/* Call on the same UI thread after lv_unlock. No file IO occurs in event callbacks. */
void stock_perf_poll(void);

#endif
