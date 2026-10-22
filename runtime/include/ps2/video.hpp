#pragma once

#include <cstdint>

namespace ps2 {

// Nominal analog-mode timings and blanking intervals from PCSX2's PS2-test-based
// Counters.cpp model. Non-interlaced analog output is double-strike (240p/288p).
struct VideoTiming {
    std::uint32_t rate = 5000, rate_den = 100;
    std::uint32_t scanlines = 625; // Total across two field periods.
    std::uint32_t blank_half_lines = 49, gs_half_lines = 6;
    bool interlaced = true, supported = true;

    constexpr std::uint64_t field_cycles(std::uint64_t clock, std::uint64_t index) const {
        const std::uint64_t numerator = clock * rate_den;
        const std::uint64_t remainder = numerator % rate, phase = index % rate;
        return numerator / rate + ((phase + 1) * remainder) / rate - (phase * remainder) / rate;
    }

    constexpr std::uint64_t half_lines_cycles(std::uint64_t clock, unsigned count) const {
        return clock * rate_den * count / (static_cast<std::uint64_t>(rate) * scanlines);
    }

    constexpr double field_seconds() const { return static_cast<double>(rate_den) / rate; }

    constexpr std::uint64_t scanlines_elapsed(std::uint64_t clock, std::uint64_t cycles) const {
        const std::uint64_t numerator = clock * rate_den * 2, denominator = static_cast<std::uint64_t>(rate) * scanlines;
        return (cycles / numerator) * denominator + (cycles % numerator) * denominator / numerator;
    }

    constexpr std::uint64_t cycles_for_scanlines(std::uint64_t clock, std::uint64_t ticks) const {
        const std::uint64_t numerator = clock * rate_den * 2, denominator = static_cast<std::uint64_t>(rate) * scanlines;
        return (ticks / denominator) * numerator + ((ticks % denominator) * numerator + denominator - 1) / denominator;
    }
};

constexpr VideoTiming video_timing_for(bool pal, bool interlaced, std::uint32_t mode = 0) {
    if (mode == 0x50) return {5994, 100, 1050, 90, 14, false, true}; // 480p: 525 full lines per period.
    const bool supported = mode == 0 || mode == 2 || mode == 3;
    if (pal) return interlaced ? VideoTiming{5000, 100, 625, 49, 6, true, supported} :
                                VideoTiming{4976, 100, 628, 52, 9, false, supported};
    return interlaced ? VideoTiming{5994, 100, 525, 45, 7, true, supported} :
                       VideoTiming{5983, 100, 526, 46, 8, false, supported};
}

}
