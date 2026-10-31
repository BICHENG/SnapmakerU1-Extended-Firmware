// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/time.h>

// Only the screen instance exports SCREEN_TOUCH_DEVICE; camera instances have no input access.
class ScreenTouch {
    int fd_ = -1;
    input_absinfo x_{}, y_{};
    std::mutex mutex_;
    std::string owner_;
    std::chrono::steady_clock::time_point last_;
    bool event(unsigned short type, unsigned short code, int value) {
        input_event e{}; gettimeofday(&e.time, nullptr);
        e.type = type; e.code = code; e.value = value;
        return write(fd_, &e, sizeof(e)) == sizeof(e);
    }
    void release() {
        if (owner_.empty()) return;
        event(EV_ABS, ABS_MT_SLOT, 0);
        event(EV_ABS, ABS_MT_TRACKING_ID, -1);
        event(EV_KEY, BTN_TOUCH, 0); event(EV_SYN, SYN_REPORT, 0);
        owner_.clear();
    }
public:
    ScreenTouch() {
        const char *device = getenv("SCREEN_TOUCH_DEVICE");
        if (!device) return;
        fd_ = open(device, O_WRONLY | O_CLOEXEC);
        if (fd_ < 0 || ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_X), &x_) ||
            ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_Y), &y_)) {
            perror("screen touch"); if (fd_ >= 0) close(fd_); fd_ = -1;
        }
    }
    ~ScreenTouch() { release(); if (fd_ >= 0) close(fd_); }
    void expire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner_.empty() && std::chrono::steady_clock::now() - last_ > std::chrono::seconds(2)) release();
    }
    void disconnected(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (owner_ == id) release();
    }
    bool message(const std::string& id, const std::string& text) {
        if (fd_ < 0 || text.size() > 256) return false;
        auto msg = nlohmann::json::parse(text, nullptr, false);
        if (!msg.is_object() || !msg.contains("touch") || !msg["touch"].is_string()) return false;
        auto action = msg["touch"].get<std::string>();
        std::lock_guard<std::mutex> lock(mutex_);
        if (action == "hold") {
            if (owner_.empty() || owner_ != id) return false;
            last_ = std::chrono::steady_clock::now();
            return true;
        }
        if (action == "up") { if (owner_ != id) return false; release(); return true; }
        if (action != "down" && action != "move") return false;
        if (!msg.contains("x") || !msg.contains("y") || !msg["x"].is_number() || !msg["y"].is_number()) return false;
        double x = msg["x"].get<double>(), y = msg["y"].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return false;
        if (action == "down") {
            if (!owner_.empty()) return false;
            owner_ = id; event(EV_ABS, ABS_MT_SLOT, 0); event(EV_ABS, ABS_MT_TRACKING_ID, 1);
        } else if (owner_ != id) return false;
        bool ok = event(EV_ABS, ABS_MT_POSITION_X, x_.minimum + std::lround(x * (x_.maximum-x_.minimum)));
        ok &= event(EV_ABS, ABS_MT_POSITION_Y, y_.minimum + std::lround(y * (y_.maximum-y_.minimum)));
        if (action == "down") ok &= event(EV_KEY, BTN_TOUCH, 1);
        ok &= event(EV_SYN, SYN_REPORT, 0);
        last_ = std::chrono::steady_clock::now();
        if (!ok) release();
        return ok;
    }
};
static ScreenTouch screen_touch;
