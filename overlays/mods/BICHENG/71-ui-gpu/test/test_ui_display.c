#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
static int test_ioctl(int fd, unsigned long request, ...);
#define ioctl test_ioctl
#include "../apps/ui-display/ui-display.c"
#undef ioctl

static ui_display *subject;
static unsigned front, calls;
static int fail_pan;
static int test_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == subject->fd);
    va_list args; va_start(args, request);
    struct fb_var_screeninfo *var = va_arg(args, struct fb_var_screeninfo *);
    va_end(args);
    if(request == FBIOGET_VSCREENINFO) { *var = subject->var; var->yoffset = front; return 0; }
    assert(request == FBIOPAN_DISPLAY && var->yoffset != front);
    assert(var->activate == FB_ACTIVATE_VBL);
    assert(!memcmp(subject->scanout + var->yoffset * UI_FRAME_STRIDE, subject->canvas, UI_FRAME_BYTES));
    calls++;
    if(fail_pan) { errno = EBUSY; return -1; }
    front = var->yoffset;
    return 0;
}

int main(void)
{
    ui_display d = {0}; subject = &d;
    d.fd = 99; d.power_fd = -1; d.double_buffer = 1; d.fix.line_length = UI_FRAME_STRIDE;
    d.var.xres = UI_FRAME_WIDTH; d.var.yres = UI_FRAME_HEIGHT;
    d.var.yres_virtual = UI_FRAME_HEIGHT * 2;
    d.scanout = calloc(2, UI_FRAME_BYTES); assert(d.scanout);
    char path[] = "/tmp/ui-frame-test.XXXXXX";
    d.snapshot_fd = mkstemp(path); assert(d.snapshot_fd >= 0);
    assert(!ftruncate(d.snapshot_fd, sizeof(struct ui_frame)));
    d.snapshot = mmap(NULL, sizeof(struct ui_frame), PROT_READ | PROT_WRITE, MAP_SHARED, d.snapshot_fd, 0);
    assert(d.snapshot != MAP_FAILED);
    d.report_ns = now_ns();
    uint8_t row[16]; memset(row, 42, sizeof(row));
    assert(!ui_display_update(&d, 1, 1, 4, 1, row, sizeof(row), 0));
    assert(!calls && !d.frames && !d.snapshot->magic);
    assert(d.scanout[UI_FRAME_STRIDE + 4] == 0);
    assert(!ui_display_update(&d, 1, 2, 4, 2, row, sizeof(row), 1));
    assert(calls == 1 && front == 320 && d.flips == 1 && d.published == 1);
    assert(d.scanout[UI_FRAME_STRIDE + 4] == 0);
    assert(d.snapshot->pixels[UI_FRAME_STRIDE + 4] == 42 && d.snapshot->sequence == 1);
    assert(d.snapshot->pixels[2 * UI_FRAME_STRIDE + 4] == 42);
    assert(!ui_display_update(&d, 1, 3, 4, 3, row, sizeof(row), 1));
    assert(calls == 2 && front == 0 && d.flips == 2);
    assert(d.scanout[UI_FRAME_BYTES + 3 * UI_FRAME_STRIDE + 4] == 0);
    assert(ui_display_update(&d, -1, 0, 0, 0, row, 4, 1) == -1);
    int ready[2], release[2]; assert(!pipe(ready) && !pipe(release));
    pid_t reader = fork(); assert(reader >= 0);
    if(!reader) {
        assert(!ui_frame_lock(d.snapshot_fd, F_RDLCK));
        assert(write(ready[1], "x", 1) == 1);
        char byte; assert(read(release[0], &byte, 1) == 1);
        assert(!ui_frame_lock(d.snapshot_fd, F_UNLCK));
        _exit(0);
    }
    char byte; assert(read(ready[0], &byte, 1) == 1);
    assert(!ui_display_update(&d, 1, 4, 4, 4, row, sizeof(row), 1));
    assert(d.flips == 3 && d.publish_skips == 1 && d.snapshot->sequence == 2);
    assert(write(release[1], "x", 1) == 1);
    int status; assert(waitpid(reader, &status, 0) == reader && status == 0);
    fail_pan = 1;
    assert(!ui_display_update(&d, 1, 5, 4, 5, row, sizeof(row), 1));
    assert(d.errors == 1 && d.flips == 3 && d.snapshot->sequence == 4);
    assert(d.scanout[front * UI_FRAME_STRIDE + 5 * UI_FRAME_STRIDE + 4] == 42);
    char power_path[] = "/tmp/ui-power-test.XXXXXX";
    d.power_fd = mkstemp(power_path); assert(d.power_fd >= 0);
    assert(write(d.power_fd, "4\n", 2) == 2);
    unsigned asleep_calls = calls;
    assert(!ui_display_update(&d, 1, 6, 4, 6, row, sizeof(row), 1));
    assert(calls == asleep_calls && d.snapshot->sequence == 5);
    assert(pwrite(d.power_fd, "0\n", 2, 0) == 2);
    fail_pan = 0; front = 0; // The backend resets the scanout page on wake.
    assert(!ui_display_update(&d, 1, 7, 4, 7, row, sizeof(row), 1));
    assert(calls == asleep_calls + 1 && front == 320);
    close(d.power_fd); unlink(power_path);
    munmap(d.snapshot, sizeof(struct ui_frame)); close(d.snapshot_fd); unlink(path); free(d.scanout);
    puts("display tests passed: complete refresh, back-page ownership, atomic flip, nonblocking snapshot, fallback");
}
