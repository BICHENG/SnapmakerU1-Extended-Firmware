/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "screen_packet.h"
#include "log.h"
#include <cerrno>
#include <vector>
#include <ctime>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

struct h264_stream_t {
    int fd = -1;
    std::vector<uint8_t> buf;
    uint64_t last_ns = 0;
};
#define H264_STREAM_INIT {}

static bool h264_stream_close(h264_stream_t *s)
{
    bool was_open = s->fd >= 0;
    if(was_open) close(s->fd);
    s->fd = -1; s->buf.clear(); s->last_ns = 0;
    return was_open;
}

static bool h264_stream_open(h264_stream_t *s, const char *path)
{
    if(s->fd >= 0) return false;
    s->buf.clear(); s->last_ns = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) return false;
    sockaddr_un addr{}; addr.sun_family = AF_UNIX;
    if(strlen(path) >= sizeof(addr.sun_path)) { close(fd); return false; }
    strcpy(addr.sun_path, path);
    if(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) { close(fd); return false; }
    s->fd = fd;
    return true;
}

static ssize_t h264_stream_process(h264_stream_t *s,
                                   void (*frame)(const uint8_t*, size_t, uint64_t))
{
    if(s->fd < 0) return -1;
    uint8_t incoming[65536];
    ssize_t count = recv(s->fd, incoming, sizeof(incoming), MSG_DONTWAIT);
    if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    if(count <= 0) { h264_stream_close(s); return -1; }
    s->buf.insert(s->buf.end(), incoming, incoming + count);
    size_t consumed = 0;
    while(s->buf.size() - consumed >= SCREEN_PACKET_HEADER) {
        uint32_t size; uint64_t ns;
        if(!screen_packet_read(s->buf.data() + consumed, &size, &ns) || ns <= s->last_ns) {
            log_errorf("Invalid screen frame record; reconnecting for an IDR\n");
            h264_stream_close(s); return -1;
        }
        if(s->buf.size() - consumed < SCREEN_PACKET_HEADER + size) break;
        timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t current = uint64_t(now.tv_sec) * 1000000000u + now.tv_nsec;
        if(ns > current || current - ns > 250000000u) {
            // Dropping individual P-frames corrupts prediction; reconnect at an IDR instead.
            log_errorf("Stale screen stream; reconnecting for an IDR\n");
            h264_stream_close(s); return -1;
        }
        frame(s->buf.data() + consumed + SCREEN_PACKET_HEADER, size, ns);
        s->last_ns = ns;
        consumed += SCREEN_PACKET_HEADER + size;
    }
    if(consumed) s->buf.erase(s->buf.begin(), s->buf.begin() + consumed);
    return count;
}
