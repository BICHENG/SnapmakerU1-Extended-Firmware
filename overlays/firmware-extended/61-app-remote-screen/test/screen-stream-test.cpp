#include <cassert>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include "../apps/screen_stream.h"

static std::vector<uint64_t> seen;
static void receive_frame(const uint8_t *data, size_t size, uint64_t pts)
{
    assert(size == 7 && data[0] == 0 && data[3] == 1 && data[4] == 0x65);
    seen.push_back(pts);
}
static uint64_t now_ns()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000u + ts.tv_nsec;
}
static std::vector<uint8_t> records(uint64_t pts)
{
    std::vector<uint8_t> bytes;
    for(unsigned n = 0; n < 3; n++) {
        uint8_t packet[23] = {0};
        screen_packet_header(packet, 7, pts + n);
        const uint8_t payload[] = {0, 0, 0, 1, 0x65, 0x88, 0x80};
        memcpy(packet + 16, payload, 7);
        bytes.insert(bytes.end(), packet, packet + sizeof(packet));
    }
    return bytes;
}
int main()
{
    for(unsigned chunk = 1; chunk <= 69; chunk++) {
        int fds[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        h264_stream_t stream; stream.fd = fds[1];
        uint64_t pts = now_ns() - 10000;
        auto data = records(pts); seen.clear();
        for(size_t pos = 0; pos < data.size();) {
            auto count = std::min(size_t(chunk), data.size() - pos);
            assert(write(fds[0], data.data() + pos, count) == ssize_t(count));
            assert(h264_stream_process(&stream, receive_frame) > 0);
            pos += count;
        }
        assert((seen == std::vector<uint64_t>{pts, pts + 1, pts + 2}));
        assert(stream.buf.empty());
        close(fds[0]);
        assert(h264_stream_process(&stream, receive_frame) == -1 && stream.fd == -1);
    }
    for(unsigned kind = 0; kind < 4; kind++) {
        int fds[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
        h264_stream_t stream; stream.fd = fds[1];
        auto data = records(now_ns() - 10000);
        if(kind == 0) data[0] = 0;
        if(kind == 1) screen_packet_header(data.data(), SCREEN_PACKET_MAX + 1, now_ns());
        if(kind == 2) screen_packet_header(data.data(), 7, now_ns() - 300000000u);
        if(kind == 3) stream.last_ns = now_ns();
        assert(write(fds[0], data.data(), data.size()) == ssize_t(data.size()));
        assert(h264_stream_process(&stream, receive_frame) == -1 && stream.fd == -1);
        assert(stream.buf.empty()); close(fds[0]);
    }
    h264_stream_t missing;
    assert(!h264_stream_open(&missing, "/this-screen-socket-does-not-exist"));
    puts("screen stream tests passed: all fragmentation/coalescing boundaries, PTS, bounds, stale/reordered frames, EOF");
}
