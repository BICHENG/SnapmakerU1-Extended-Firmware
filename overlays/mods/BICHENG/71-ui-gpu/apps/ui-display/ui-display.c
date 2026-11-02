/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "ui-display.h"
#include "ui-frame.h"
#include <errno.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct ui_display {
    int fd, snapshot_fd, power_fd, double_buffer;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    uint8_t *scanout;
    struct ui_frame *snapshot;
    uint8_t canvas[UI_FRAME_BYTES];
    uint64_t frames, flips, errors, published, publish_skips, report_ns, flip_ns_max;
    uint64_t interruptions, retries, last_error_frame, last_error_ns;
    const char *last_error_op;
    int last_error_errno, last_error_recovered;
    unsigned last_error_retries;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static void copy_page(ui_display *d, unsigned yoffset)
{
    for(unsigned y = 0; y < UI_FRAME_HEIGHT; y++)
        memcpy(d->scanout + (size_t)(y + yoffset) * d->fix.line_length,
               d->canvas + y * UI_FRAME_STRIDE, UI_FRAME_STRIDE);
}

static int panel_awake(ui_display *d)
{
    char value[8];
    return d->power_fd < 0 || pread(d->power_fd, value, sizeof(value), 0) <= 0 || value[0] == '0';
}

static void snapshot_open(ui_display *d)
{
    int fd = open(UI_FRAME_PATH, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    if(fd < 0) return;
    struct stat st;
    if(fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
       (st.st_size && st.st_size != sizeof(struct ui_frame)) ||
       flock(fd, LOCK_EX | LOCK_NB) || ui_frame_lock(fd, F_WRLCK)) {
        close(fd);
        return;
    }
    if(ftruncate(fd, sizeof(struct ui_frame))) { close(fd); return; }
    void *map = mmap(NULL, sizeof(struct ui_frame), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(map == MAP_FAILED) { close(fd); return; }
    d->snapshot = map;
    d->snapshot_fd = fd;
    d->snapshot->magic = 0;
    ui_frame_lock(fd, F_UNLCK);
}

static void publish(ui_display *d, uint64_t timestamp)
{
    if(!d->snapshot) snapshot_open(d);
    if(!d->snapshot || ui_frame_lock(d->snapshot_fd, F_WRLCK)) {
        d->publish_skips++;
        return;
    }
    memcpy(d->snapshot->pixels, d->canvas, UI_FRAME_BYTES);
    d->snapshot->width = UI_FRAME_WIDTH;
    d->snapshot->height = UI_FRAME_HEIGHT;
    d->snapshot->stride = UI_FRAME_STRIDE;
    d->snapshot->sequence = d->frames;
    d->snapshot->monotonic_ns = timestamp;
    d->snapshot->producer_pid = (uint32_t)getpid();
    d->snapshot->magic = UI_FRAME_MAGIC;
    ui_frame_lock(d->snapshot_fd, F_UNLCK);
    d->published++;
}

static void report(ui_display *d, uint64_t now)
{
    if(d->report_ns && now - d->report_ns < UINT64_C(5000000000)) return;
    d->report_ns = now;
    FILE *file = fopen("/tmp/ui-display.json.tmp", "w");
    if(!file) return;
    fprintf(file, "{\"pid\":%ld,\"double_buffer\":%s,\"frames\":%llu,\"flips\":%llu,"
            "\"errors\":%llu,\"published\":%llu,\"publish_skips\":%llu,"
            "\"flip_ns_max\":%llu,\"yoffset\":%u,\"monotonic_ns\":%llu,"
            "\"interruptions\":%llu,\"retries\":%llu,\"last_error\":{\"op\":\"%s\","
            "\"errno\":%d,\"frame\":%llu,\"monotonic_ns\":%llu,\"retries\":%u,\"recovered\":%s}}\n",
            (long)getpid(), d->double_buffer ? "true" : "false",
            (unsigned long long)d->frames, (unsigned long long)d->flips,
            (unsigned long long)d->errors, (unsigned long long)d->published,
            (unsigned long long)d->publish_skips, (unsigned long long)d->flip_ns_max,
            d->var.yoffset, (unsigned long long)now,
            (unsigned long long)d->interruptions, (unsigned long long)d->retries,
            d->last_error_op ? d->last_error_op : "", d->last_error_errno,
            (unsigned long long)d->last_error_frame, (unsigned long long)d->last_error_ns,
            d->last_error_retries, d->last_error_recovered ? "true" : "false");
    if(fclose(file) == 0) rename("/tmp/ui-display.json.tmp", "/tmp/ui-display.json");
}

static void note_failure(ui_display *d, const char *op, int error, unsigned retries)
{
    d->last_error_op = op;
    d->last_error_errno = error;
    d->last_error_frame = d->frames;
    d->last_error_ns = now_ns();
    d->last_error_retries = retries;
    d->last_error_recovered = 0;
    if(error == EINTR) d->interruptions++;
    d->report_ns = 0;
}

ui_display *ui_display_open(const char *device)
{
    ui_display *d = calloc(1, sizeof(*d));
    if(!d) return NULL;
    d->fd = d->snapshot_fd = d->power_fd = -1;
    d->fd = open(device, O_RDWR | O_CLOEXEC);
    if(d->fd < 0 || ioctl(d->fd, FBIOGET_FSCREENINFO, &d->fix) ||
       ioctl(d->fd, FBIOGET_VSCREENINFO, &d->var)) goto fail;
    if(d->var.xres != UI_FRAME_WIDTH || d->var.yres != UI_FRAME_HEIGHT ||
       d->var.bits_per_pixel != 32 || d->var.red.offset != 16 ||
       d->var.green.offset != 8 || d->var.blue.offset || d->var.xoffset ||
       d->fix.line_length < UI_FRAME_STRIDE ||
       d->var.yres_virtual < d->var.yres || d->var.yoffset > d->var.yres_virtual - d->var.yres ||
       (uint64_t)d->fix.line_length * d->var.yres_virtual > d->fix.smem_len) goto fail;
    d->scanout = mmap(NULL, d->fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, 0);
    if(d->scanout == MAP_FAILED) { d->scanout = NULL; goto fail; }
    for(unsigned y = 0; y < UI_FRAME_HEIGHT; y++)
        memcpy(d->canvas + y * UI_FRAME_STRIDE,
               d->scanout + (size_t)(y + d->var.yoffset) * d->fix.line_length, UI_FRAME_STRIDE);
    d->double_buffer = d->var.yres_virtual >= UI_FRAME_HEIGHT * 2 && d->fix.ypanstep &&
                       UI_FRAME_HEIGHT % d->fix.ypanstep == 0 &&
                       (d->var.yoffset == 0 || d->var.yoffset == UI_FRAME_HEIGHT);
    d->power_fd = open("/sys/class/backlight/backlight/bl_power", O_RDONLY | O_CLOEXEC);
    snapshot_open(d);
    fprintf(stderr, "UI display: %ux%u virtual=%ux%u mode=%s\n",
            d->var.xres, d->var.yres, d->var.xres_virtual, d->var.yres_virtual,
            d->double_buffer ? "page-flip" : "single-page-unsynchronized");
    return d;
fail:
    ui_display_close(d);
    return NULL;
}

int ui_display_update(ui_display *d, int x1, int y1, int x2, int y2,
                      const uint8_t *pixels, unsigned stride, int last)
{
    if(!d || !pixels || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
       x2 >= UI_FRAME_WIDTH || y2 >= UI_FRAME_HEIGHT || stride < (unsigned)(x2-x1+1)*4)
        return -1;
    for(int y = y1; y <= y2; y++)
        memcpy(d->canvas + y * UI_FRAME_STRIDE + x1 * 4,
               pixels + (size_t)(y-y1) * stride, (size_t)(x2-x1+1)*4);
    if(!last) return 0;
    d->frames++;
    if(!panel_awake(d)) goto completed;
    struct fb_var_screeninfo actual;
    if(ioctl(d->fd, FBIOGET_VSCREENINFO, &actual)) {
        note_failure(d, "FBIOGET_VSCREENINFO", errno, 0);
        d->errors++;
        goto completed;
    }
    if(actual.xres != UI_FRAME_WIDTH || actual.yres != UI_FRAME_HEIGHT || actual.xoffset ||
       (actual.yoffset != 0 && actual.yoffset != UI_FRAME_HEIGHT)) {
        note_failure(d, "FBIOGET_VSCREENINFO.validate", EINVAL, 0);
        d->errors++;
        goto completed;
    }
    d->var.yoffset = actual.yoffset;
    if(d->double_buffer) {
        unsigned back = d->var.yoffset ? 0 : UI_FRAME_HEIGHT;
        copy_page(d, back);
        atomic_thread_fence(memory_order_seq_cst);
        struct fb_var_screeninfo next = d->var;
        next.yoffset = back;
        next.activate = FB_ACTIVATE_VBL;
        uint64_t before = now_ns();
        unsigned retries = 0;
        int result;
        /* The board's DRM fbdev helper uses a blocking atomic commit. */
        for(;;) {
            result = ioctl(d->fd, FBIOPAN_DISPLAY, &next);
            if(result == 0) break;
            int error = errno;
            note_failure(d, "FBIOPAN_DISPLAY", error, retries);
            if(error != EINTR) break;
            if(retries == 2) break;
            retries++;
            d->retries++;
        }
        if(retries) {
            d->last_error_retries = retries;
            d->last_error_recovered = result == 0;
        }
        if(result == 0) {
            d->var = next;
            d->flips++;
            uint64_t elapsed = now_ns() - before;
            if(elapsed > d->flip_ns_max) d->flip_ns_max = elapsed;
        }
        else {
            d->errors++;
            /* Keep the visible page intact; the next flush submits the complete canvas. */
        }
    }
    else copy_page(d, d->var.yoffset);
completed:;
    uint64_t timestamp = now_ns();
    publish(d, timestamp);
    report(d, timestamp);
    return 0;
}

void ui_display_close(ui_display *d)
{
    if(!d) return;
    if(d->scanout) {
        if(d->var.yoffset && panel_awake(d)) {
            copy_page(d, 0);
            d->var.yoffset = 0;
            d->var.activate = FB_ACTIVATE_VBL;
            ioctl(d->fd, FBIOPAN_DISPLAY, &d->var);
        }
        munmap(d->scanout, d->fix.smem_len);
    }
    if(d->snapshot) munmap(d->snapshot, sizeof(struct ui_frame));
    if(d->snapshot_fd >= 0) close(d->snapshot_fd);
    if(d->power_fd >= 0) close(d->power_fd);
    if(d->fd >= 0) close(d->fd);
    free(d);
}
