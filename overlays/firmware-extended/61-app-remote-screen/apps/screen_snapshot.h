/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SCREEN_SNAPSHOT_H
#define SCREEN_SNAPSHOT_H
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ui-frame.h"

/* One complete new frame, zero when unchanged/busy, or -1 for legacy UIs. */
static inline int screen_snapshot_read(int *fd, const struct ui_frame **map,
                                      uint64_t *last_ns, uint8_t *pixels, int force) {
    if (*fd < 0) {
        *fd = open(UI_FRAME_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (*fd < 0) return -1;
        struct stat st;
        if (fstat(*fd, &st) || !S_ISREG(st.st_mode) || st.st_size != sizeof(struct ui_frame)) {
            close(*fd); *fd = -1; return -1;
        }
        void *data = mmap(NULL, sizeof(struct ui_frame), PROT_READ, MAP_SHARED, *fd, 0);
        if (data == MAP_FAILED) { close(*fd); *fd = -1; return -1; }
        *map = data;
    }
    /* The UI process can survive the presenter: only its exclusive lock proves ownership. */
    if (flock(*fd, LOCK_SH | LOCK_NB) == 0) {
        flock(*fd, LOCK_UN);
        return -1;
    }
    if (errno != EWOULDBLOCK && errno != EAGAIN) return -1;
    if (ui_frame_lock(*fd, F_RDLCK)) return 0;
    const struct ui_frame *frame = *map;
    int result = -1;
    if (frame->magic == UI_FRAME_MAGIC && frame->width == UI_FRAME_WIDTH &&
        frame->height == UI_FRAME_HEIGHT && frame->stride == UI_FRAME_STRIDE &&
        frame->monotonic_ns && frame->producer_pid > 0 && kill(frame->producer_pid, 0) == 0) {
        result = force || frame->monotonic_ns != *last_ns;
        if (result) {
            memcpy(pixels, frame->pixels, UI_FRAME_BYTES);
            *last_ns = frame->monotonic_ns;
        }
    }
    ui_frame_lock(*fd, F_UNLCK);
    return result;
}
#endif
