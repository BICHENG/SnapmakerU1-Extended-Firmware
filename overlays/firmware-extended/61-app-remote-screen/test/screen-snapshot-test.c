/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdlib.h>
#include "screen_snapshot.h"

int main(void) {
    char path[] = "/tmp/screen-snapshot-XXXXXX";
    int producer = mkstemp(path);
    assert(producer >= 0);
    assert(ftruncate(producer, sizeof(struct ui_frame)) == 0);
    struct ui_frame *frame = mmap(NULL, sizeof(*frame), PROT_READ | PROT_WRITE,
                                 MAP_SHARED, producer, 0);
    assert(frame != MAP_FAILED);
    int reader = open(path, O_RDONLY | O_CLOEXEC);
    assert(reader >= 0);
    const struct ui_frame *snapshot = mmap(NULL, sizeof(*frame), PROT_READ,
                                          MAP_SHARED, reader, 0);
    assert(snapshot != MAP_FAILED);
    assert(unlink(path) == 0);
    uint8_t *pixels = malloc(UI_FRAME_BYTES);
    assert(pixels);
    uint64_t last_ns = 0;
    frame->magic = UI_FRAME_MAGIC;
    frame->width = UI_FRAME_WIDTH;
    frame->height = UI_FRAME_HEIGHT;
    frame->stride = UI_FRAME_STRIDE;
    frame->producer_pid = getpid();
    frame->monotonic_ns = 1;
    memset(frame->pixels, 42, UI_FRAME_BYTES);
    assert(flock(producer, LOCK_EX | LOCK_NB) == 0);
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 0) == 1);
    assert(last_ns == 1 && pixels[0] == 42 && pixels[UI_FRAME_BYTES - 1] == 42);
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 0) == 0);
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 1) == 1);

    /* Backend teardown must fall back even while the owning UI process is alive. */
    assert(flock(producer, LOCK_UN) == 0);
    assert(kill(frame->producer_pid, 0) == 0);
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 1) == -1);
    assert(flock(producer, LOCK_EX | LOCK_NB) == 0);
    frame->monotonic_ns = 2;
    frame->pixels[0] = 84;
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 0) == 1);
    assert(last_ns == 2 && pixels[0] == 84);
    assert(munmap(frame, sizeof(*frame)) == 0);
    assert(close(producer) == 0);
    assert(screen_snapshot_read(&reader, &snapshot, &last_ns, pixels, 1) == -1);
    munmap((void *)snapshot, sizeof(*snapshot));
    close(reader);
    free(pixels);
    return 0;
}
