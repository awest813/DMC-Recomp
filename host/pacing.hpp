#pragma once

#include <cstdint>

namespace native_host {

struct FrameDeadline {
    double next = 0.0, period = 0.0;
    double advance(double now, double step, double adjustment = 0.0) {
        if (!next || period != step || now > next + step * 4) next = now;
        period = step;
        next += step + adjustment;
        return next;
    }
    void reset() { next = period = 0.0; }
};

struct AudioProgress {
    std::uint32_t previous, changed;
    bool stalled(std::uint32_t queued, std::uint32_t now) {
        if (queued < previous) changed = now;
        previous = queued;
        return now - changed >= 250;
    }
};

}
