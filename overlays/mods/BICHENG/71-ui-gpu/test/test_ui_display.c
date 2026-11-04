#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
static int test_ioctl(int fd, unsigned long request, ...);
static FILE *test_fopen(const char *path, const char *mode);
static int test_rename(const char *from, const char *to);
#define ioctl test_ioctl
#define fopen test_fopen
#define rename test_rename
#include "../apps/ui-display/ui-display.c"
#undef ioctl
#undef fopen
#undef rename

static ui_display *subject;
static unsigned front, calls;
static unsigned pan_failures, report_writes;
static int pan_error, get_error, invalid_geometry;
static int block_pan_once, pan_entered[2], pan_release[2];
static char report_path[] = "/tmp/ui-display-test.XXXXXX";
static char report_tmp[sizeof(report_path)+4];
static FILE *test_fopen(const char *path, const char *mode)
{
    assert(!strcmp(path, "/tmp/ui-display.json.tmp"));
    report_writes++;
    return fopen(report_tmp, mode);
}
static int test_rename(const char *from, const char *to)
{
    assert(!strcmp(from, "/tmp/ui-display.json.tmp") && !strcmp(to, "/tmp/ui-display.json"));
    return rename(report_tmp, report_path);
}
static void check_report(const char *op, int error, unsigned frame, unsigned retries, int recovered)
{
    char json[1024], expected[128];
    FILE *file = fopen(report_path, "r"); assert(file);
    size_t count = fread(json, 1, sizeof(json)-1, file); json[count] = 0; fclose(file);
    const char *last = strstr(json, "\"last_error\":"); assert(last);
    snprintf(expected, sizeof(expected), "\"op\":\"%s\",\"errno\":%d,\"frame\":%u,", op, error, frame);
    assert(strstr(last, expected));
    snprintf(expected, sizeof(expected), "\"retries\":%u,\"recovered\":%s", retries, recovered ? "true" : "false");
    assert(strstr(last, expected));
    const char *timestamp = strstr(last, "\"monotonic_ns\":"); assert(timestamp);
    unsigned long long ns = 0; assert(sscanf(timestamp, "\"monotonic_ns\":%llu", &ns) == 1 && ns);
}
static int test_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == subject->fd);
    va_list args; va_start(args, request);
    struct fb_var_screeninfo *var = va_arg(args, struct fb_var_screeninfo *);
    va_end(args);
    if(request == FBIOGET_VSCREENINFO) {
        if(get_error) { errno = get_error; return -1; }
        *var = subject->var; var->yoffset = front;
        if(invalid_geometry) { var->xres++; errno = EBUSY; }
        return 0;
    }
    assert(request == FBIOPAN_DISPLAY && var->yoffset != front);
    assert(var->yoffset == 0 || var->yoffset == UI_FRAME_HEIGHT);
    assert(var->activate == FB_ACTIVATE_VBL);
    assert(!memcmp(subject->scanout + var->yoffset * UI_FRAME_STRIDE, frame_pixels(subject), UI_FRAME_BYTES));
    calls++;
    if(block_pan_once) {
        block_pan_once = 0;
        assert(write(pan_entered[1], "x", 1) == 1);
        char byte; assert(read(pan_release[0], &byte, 1) == 1);
    }
    if(pan_failures) { pan_failures--; errno = pan_error; return -1; }
    front = var->yoffset;
    return 0;
}

int main(void)
{
    ui_display d = {0}; subject = &d;
    int report_fd = mkstemp(report_path); assert(report_fd >= 0); close(report_fd);
    snprintf(report_tmp, sizeof(report_tmp), "%s.tmp", report_path);
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
    unsigned before = calls;
    pan_error = EINTR; pan_failures = 1;
    assert(!ui_display_update(&d, 1, 5, 4, 5, row, sizeof(row), 1));
    assert(calls == before + 2 && d.errors == 0 && d.flips == 4 && d.snapshot->sequence == 4);
    assert(d.interruptions == 1 && d.retries == 1 && report_writes == 1);
    assert(d.scanout[front * UI_FRAME_STRIDE + 5 * UI_FRAME_STRIDE + 4] == 42);
    check_report("FBIOPAN_DISPLAY", EINTR, 4, 1, 1);

    uint8_t *visible = malloc(UI_FRAME_BYTES); assert(visible);
    memcpy(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES);
    unsigned old_front = front; before = calls; pan_failures = 3;
    assert(!ui_display_update(&d, 1, 6, 4, 6, row, sizeof(row), 1));
    assert(calls == before + 3 && front == old_front && d.flips == 4 && d.errors == 1);
    assert(!memcmp(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));
    assert(d.snapshot->sequence == 5 && d.snapshot->pixels[6 * UI_FRAME_STRIDE + 4] == 42);
    assert(d.interruptions == 4 && d.retries == 3 && report_writes == 2);
    check_report("FBIOPAN_DISPLAY", EINTR, 5, 2, 0);
    assert(!ui_display_update(&d, 1, 7, 4, 7, row, sizeof(row), 1));
    assert(front != old_front && d.flips == 5 && d.errors == 1);
    assert(!memcmp(d.canvas, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));

    memcpy(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES);
    old_front = front; before = calls; pan_error = EBUSY; pan_failures = 5;
    assert(!ui_display_update(&d, 1, 8, 4, 8, row, sizeof(row), 1));
    assert(calls == before + 1 && pan_failures == 4 && front == old_front && d.errors == 2 && d.flips == 5);
    assert(d.retries == 3 && !memcmp(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));
    assert(report_writes == 3); check_report("FBIOPAN_DISPLAY", EBUSY, 7, 0, 0);
    pan_failures = 0;
    assert(!ui_display_update(&d, 1, 9, 4, 9, row, sizeof(row), 1));
    assert(d.flips == 6 && !memcmp(d.canvas, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));

    memcpy(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES);
    before = calls; get_error = ENODEV;
    assert(!ui_display_update(&d, 1, 10, 4, 10, row, sizeof(row), 1));
    assert(calls == before && d.errors == 3 && report_writes == 4);
    check_report("FBIOGET_VSCREENINFO", ENODEV, 9, 0, 0);
    get_error = 0; invalid_geometry = 1;
    assert(!ui_display_update(&d, 1, 11, 4, 11, row, sizeof(row), 1));
    assert(calls == before && d.errors == 4 && report_writes == 5);
    assert(!memcmp(visible, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));
    check_report("FBIOGET_VSCREENINFO.validate", EINVAL, 10, 0, 0);
    invalid_geometry = 0;
    assert(!ui_display_update(&d, 1, 12, 4, 12, row, sizeof(row), 1));
    assert(d.flips == 7 && !memcmp(d.canvas, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));

    char power_path[] = "/tmp/ui-power-test.XXXXXX";
    d.power_fd = mkstemp(power_path); assert(d.power_fd >= 0);
    assert(write(d.power_fd, "4\n", 2) == 2);
    unsigned asleep_calls = calls;
    assert(!ui_display_update(&d, 1, 13, 4, 13, row, sizeof(row), 1));
    assert(calls == asleep_calls && d.snapshot->sequence == 12);
    assert(pwrite(d.power_fd, "0\n", 2, 0) == 2);
    front = 0; // The backend resets the scanout page on wake.
    assert(!ui_display_update(&d, 1, 14, 4, 14, row, sizeof(row), 1));
    assert(calls == asleep_calls + 1 && front == 320);
    assert(!memcmp(d.canvas, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));
    d.double_buffer = 0; d.var.yres_virtual = UI_FRAME_HEIGHT; front = 0; before = calls;
    assert(!ui_display_update(&d, 1, 15, 4, 15, row, sizeof(row), 1));
    assert(calls == before && d.scanout[15 * UI_FRAME_STRIDE + 4] == 42 && d.flips == 8);
    d.double_buffer = 1; d.var.yres_virtual = UI_FRAME_HEIGHT * 2;
    assert(!pipe(pan_entered) && !pipe(pan_release));
    block_pan_once = 1;
    unsigned long long first_sequence = d.submitted;
    uint8_t *whole = malloc(UI_FRAME_BYTES); assert(whole);
    start_worker(&d); assert(d.asynchronous);
    memset(whole, 65, UI_FRAME_BYTES);
    assert(!ui_display_update(&d, 0, 0, 479, 319, whole, UI_FRAME_STRIDE, 1));
    assert(read(pan_entered[0], &byte, 1) == 1);
    for(unsigned i = 0; i < 16; ++i) {
        memset(whole, 66 + i, UI_FRAME_BYTES);
        assert(!ui_display_update(&d, 0, 0, 479, 319, whole, UI_FRAME_STRIDE, 1));
    }
    assert(d.submitted == first_sequence + 17 && d.coalesced == 15);
    assert(write(pan_release[1], "x", 1) == 1);
    stop_worker(&d);
    assert(d.flips == 10 && d.snapshot->sequence == first_sequence + 17);
    assert(!memcmp(whole, d.scanout + front * UI_FRAME_STRIDE, UI_FRAME_BYTES));
    assert(!memcmp(whole, d.snapshot->pixels, UI_FRAME_BYTES));
    for(unsigned i = 0; i < 2; ++i) { close(pan_entered[i]); close(pan_release[i]); }
    free(whole);
    close(d.power_fd); unlink(power_path);
    munmap(d.snapshot, sizeof(struct ui_frame)); close(d.snapshot_fd); unlink(path); free(d.scanout);
    unlink(report_path); unlink(report_tmp); free(visible);
    puts("display tests passed: errors, sleep, complete frames, nonblocking latest-frame delivery and shutdown drain");
}
