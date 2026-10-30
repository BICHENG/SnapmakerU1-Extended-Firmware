// SPDX-License-Identifier: GPL-3.0-or-later
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include "sock_ctx.h"

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
#define CHECK(call) do { if ((call) != MPP_OK) { fprintf(stderr, "Failed: %s\n", #call); goto done; } } while (0)

int main(int argc, char **argv) {
    const char *device = argc > 1 ? argv[1] : "/dev/fb0";
    const char *socket_path = argc > 2 ? argv[2] : "/tmp/screen-h264.sock";
    int fps = argc > 3 ? atoi(argv[3]) : 30;
    if (fps < 1 || fps > 45) return 2;
    int result = 1, fd = -1;
    unsigned char *pixels = MAP_FAILED;
    struct fb_fix_screeninfo fix = {0};
    struct fb_var_screeninfo var = {0};
    MppCtx ctx = NULL;
    MppApi *api = NULL;
    MppEncCfg cfg = NULL;
    MppBuffer buffer = NULL;
    MppFrame frame = NULL;
    sock_ctx_t socket_ctx = DEFAULT_SOCK_CTX;
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    fd = open(device, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) || ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
        perror("framebuffer"); goto done;
    }
    if (var.bits_per_pixel != 32 || var.red.offset != 16 || var.green.offset != 8 ||
        var.blue.offset != 0 || !var.xres || !var.yres || var.xres % 16 || var.yres % 16 ||
        fix.line_length < var.xres * 4 || var.xoffset || var.yoffset ||
        (size_t)fix.line_length * var.yres > fix.smem_len) {
        fprintf(stderr, "Unsupported framebuffer layout\n"); goto done;
    }
    pixels = mmap(NULL, fix.smem_len, PROT_READ, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) { perror("mmap"); goto done; }
    CHECK(mpp_create(&ctx, &api));
    CHECK(mpp_init(ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC));
    CHECK(mpp_enc_cfg_init(&cfg));
    CHECK(api->control(ctx, MPP_ENC_GET_CFG, cfg));
    mpp_enc_cfg_set_s32(cfg, "prep:width", var.xres);
    mpp_enc_cfg_set_s32(cfg, "prep:height", var.yres);
    mpp_enc_cfg_set_s32(cfg, "prep:hor_stride", var.xres * 4);
    mpp_enc_cfg_set_s32(cfg, "prep:ver_stride", var.yres);
    mpp_enc_cfg_set_s32(cfg, "prep:format", MPP_FMT_BGRA8888);
    mpp_enc_cfg_set_s32(cfg, "rc:mode", MPP_ENC_RC_MODE_CBR);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", 2000000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", 2500000);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", 500000);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(cfg, "rc:gop", fps);
    mpp_enc_cfg_set_s32(cfg, "h264:profile", 66);
    mpp_enc_cfg_set_s32(cfg, "h264:level", 31);
    mpp_enc_cfg_set_s32(cfg, "h264:cabac_en", 0);
    CHECK(api->control(ctx, MPP_ENC_SET_CFG, cfg));
    MppEncHeaderMode header = MPP_ENC_HEADER_MODE_EACH_IDR;
    CHECK(api->control(ctx, MPP_ENC_SET_HEADER_MODE, &header));
    MppBufferGroup group = NULL;
    CHECK(mpp_buffer_group_get_internal(&group, MPP_BUFFER_TYPE_DRM));
    MPP_RET allocated = mpp_buffer_get(group, &buffer, var.xres * var.yres * 4);
    mpp_buffer_group_put(group);
    CHECK(allocated);
    CHECK(mpp_frame_init(&frame));
    mpp_frame_set_width(frame, var.xres); mpp_frame_set_height(frame, var.yres);
    mpp_frame_set_hor_stride(frame, var.xres * 4); mpp_frame_set_ver_stride(frame, var.yres);
    mpp_frame_set_fmt(frame, MPP_FMT_BGRA8888); mpp_frame_set_buffer(frame, buffer);
    if (sock_open(&socket_ctx, socket_path)) goto done;
    chmod(socket_path, 0660);
    fprintf(stderr, "Framebuffer %ux%u -> MPP H264 at %d fps\n", var.xres, var.yres, fps);
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    while (running) {
        sock_accept_clients(&socket_ctx);
        if (!socket_ctx.num_clients) {
            sock_ctx_t *sockets[] = {&socket_ctx, NULL};
            sock_wait_fds(sockets, 250);
            clock_gettime(CLOCK_MONOTONIC, &next);
            continue;
        }
        unsigned char *dst = mpp_buffer_get_ptr(buffer);
        for (unsigned y = 0; y < var.yres; ++y)
            memcpy(dst + y * var.xres * 4, pixels + y * fix.line_length, var.xres * 4);
        if (socket_ctx.need_keyframe) CHECK(api->control(ctx, MPP_ENC_SET_IDR_FRAME, NULL));
        CHECK(api->encode_put_frame(ctx, frame));
        MppPacket packet = NULL;
        CHECK(api->encode_get_packet(ctx, &packet));
        if (packet) {
            static const unsigned char aud[] = {0, 0, 0, 1, 9, 0xf0};
            sock_write_cb(mpp_packet_get_pos(packet), mpp_packet_get_length(packet), &socket_ctx);
            sock_write_cb(aud, sizeof(aud), &socket_ctx);
            mpp_packet_deinit(&packet);
            socket_ctx.need_keyframe = false;
        }
        next.tv_nsec += 1000000000L / fps;
        if (next.tv_nsec >= 1000000000L) { next.tv_sec++; next.tv_nsec -= 1000000000L; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    result = 0;
done:
    sock_close(&socket_ctx);
    if (frame) mpp_frame_deinit(&frame);
    if (buffer) mpp_buffer_put(buffer);
    if (cfg) mpp_enc_cfg_deinit(cfg);
    if (ctx) mpp_destroy(ctx);
    if (pixels != MAP_FAILED) munmap(pixels, fix.smem_len);
    if (fd >= 0) close(fd);
    return result;
}
