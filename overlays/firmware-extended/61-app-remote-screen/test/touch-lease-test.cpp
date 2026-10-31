#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <nlohmann/json.hpp>

// Device operations terminate at this fake boundary; no real input device is opened.
static std::vector<input_event> events;
static char device[] = "fake-touch";
static char* fake_getenv(const char*) { return device; }
static int fake_open(const char*, int) { return 123; }
static int fake_close(int) { return 0; }
static int fake_ioctl(int, unsigned long request, input_absinfo* info) {
    info->minimum = 0;
    info->maximum = request == EVIOCGABS(ABS_MT_POSITION_X) ? 479 : 319;
    return 0;
}
static ssize_t fake_write(int, const void* buffer, size_t size) {
    assert(size == sizeof(input_event));
    events.push_back(*static_cast<const input_event*>(buffer));
    return static_cast<ssize_t>(size);
}
#define getenv fake_getenv
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define write fake_write
#include "../apps/screen_touch.h"
#undef getenv
#undef open
#undef close
#undef ioctl
#undef write

int main() {
    using namespace std::chrono_literals;
    assert(!screen_touch.message("viewer-a", R"({"touch":"hold"})"));
    assert(screen_touch.message("viewer-a", R"({"touch":"down","x":0.25,"y":0.5})"));
    const size_t down_events = events.size();
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(700ms);
        assert(screen_touch.message("viewer-a", R"({"touch":"hold"})"));
        screen_touch.expire();
        assert(events.size() == down_events); // Hold emits neither motion nor a synthetic press.
    }
    std::this_thread::sleep_for(1100ms);
    assert(!screen_touch.message("viewer-b", R"({"touch":"hold"})"));
    std::this_thread::sleep_for(1100ms);
    screen_touch.expire();
    assert(events.size() == down_events + 4); // Foreign renewal cannot prevent the original 2s expiry.
    assert(events.back().type == EV_SYN && events.back().code == SYN_REPORT);
    assert(!screen_touch.message("viewer-a", R"({"touch":"hold"})"));
    assert(!screen_touch.message("viewer-a", R"({"touch":"move","x":0.5,"y":0.5})"));
    assert(screen_touch.message("viewer-b", R"({"touch":"down","x":0.5,"y":0.5})"));
    assert(screen_touch.message("viewer-b", R"({"touch":"up"})"));
    assert(!screen_touch.message("viewer-b", R"({"touch":"hold"})"));
    std::cout << "Stationary hold, owner isolation, no synthetic events and 2s disconnect expiry passed\n";
}
