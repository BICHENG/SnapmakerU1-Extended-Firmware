/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_FRAME_H
#define UI_FRAME_H
#include <stdint.h>
#include <fcntl.h>

#define UI_FRAME_PATH "/run/ui-frame"
#define UI_FRAME_MAGIC UINT32_C(0x55494631)
#define UI_FRAME_WIDTH 480
#define UI_FRAME_HEIGHT 320
#define UI_FRAME_STRIDE (UI_FRAME_WIDTH * 4)
#define UI_FRAME_BYTES (UI_FRAME_STRIDE * UI_FRAME_HEIGHT)

/* Local, little-endian XRGB8888 snapshot, protected by a POSIX record lock. */
struct ui_frame {
    uint32_t magic, width, height, stride;
    uint64_t sequence, monotonic_ns;
    uint32_t producer_pid, reserved[7];
    uint8_t pixels[UI_FRAME_BYTES];
};

static inline int ui_frame_lock(int fd, short type)
{
    struct flock lock = {0};
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLK, &lock);
}
#endif
