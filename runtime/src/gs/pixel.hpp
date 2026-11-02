#pragma once

#include "gs.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

// Swizzled GS memory access, inline so per-pixel callers (the software rasterizer, transfers) get the
// page and block math with constant page sizes folded to shifts instead of a call and three divisions.
namespace ps2::gs::pixel {

extern std::uint8_t g_memory[kMemorySize];
extern std::uint32_t g_off32[32][64], g_offz32[32][64];
extern std::uint32_t g_off16[64][64], g_off16s[64][64], g_offz16[64][64], g_offz16s[64][64];
extern std::uint32_t g_off8[64][128], g_off4[128][128];

inline std::uint32_t wrap(std::uint32_t a) { return a & (kMemorySize - 1); }

template <typename T>
inline T get(std::uint32_t a) {
    T v;
    std::memcpy(&v, g_memory + wrap(a), sizeof v);
    return v;
}

template <typename T>
inline void put(std::uint32_t a, T v) { std::memcpy(g_memory + wrap(a), &v, sizeof v); }

template <std::uint32_t Pw, std::uint32_t Ph>
inline std::uint32_t page(std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t per_row = std::max<std::uint32_t>(1, bw * 64 / Pw);
    return bp * 256 + ((x / Pw) + (y / Ph) * per_row) * 8192;
}

[[gnu::always_inline]] inline std::uint32_t read(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y) {
    switch (psm) {
    case PSMCT32: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]);
    case PSMCT24: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]) & 0xFFFFFFu;
    case PSMZ32: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_offz32[y & 31][x & 63]);
    case PSMZ24: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_offz32[y & 31][x & 63]) & 0xFFFFFFu;
    case PSMCT16: return get<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_off16[y & 63][x & 63]);
    case PSMCT16S: return get<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_off16s[y & 63][x & 63]);
    case PSMZ16: return get<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_offz16[y & 63][x & 63]);
    case PSMZ16S: return get<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_offz16s[y & 63][x & 63]);
    case PSMT8: return get<std::uint8_t>(page<128, 64>(bp, bw, x, y) + g_off8[y & 63][x & 127]);
    case PSMT4: {
        const std::uint32_t n = g_off4[y & 127][x & 127];
        const std::uint8_t b = get<std::uint8_t>(page<128, 128>(bp, bw, x, y) + n / 2);
        return (n & 1u) ? b >> 4 : b & 15u;
    }
    case PSMT8H: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]) >> 24;
    case PSMT4HL: return (get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]) >> 24) & 15u;
    case PSMT4HH: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]) >> 28;
    default: return get<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63]);
    }
}

[[gnu::always_inline]] inline void write(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y, std::uint32_t value) {
    switch (psm) {
    case PSMCT32: put<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63], value); break;
    case PSMZ32: put<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_offz32[y & 31][x & 63], value); break;
    case PSMCT24: {
        const std::uint32_t a = page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63];
        put<std::uint32_t>(a, (get<std::uint32_t>(a) & 0xFF000000u) | (value & 0xFFFFFFu));
        break;
    }
    case PSMZ24: {
        const std::uint32_t a = page<64, 32>(bp, bw, x, y) + g_offz32[y & 31][x & 63];
        put<std::uint32_t>(a, (get<std::uint32_t>(a) & 0xFF000000u) | (value & 0xFFFFFFu));
        break;
    }
    case PSMCT16: put<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_off16[y & 63][x & 63], static_cast<std::uint16_t>(value)); break;
    case PSMCT16S: put<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_off16s[y & 63][x & 63], static_cast<std::uint16_t>(value)); break;
    case PSMZ16: put<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_offz16[y & 63][x & 63], static_cast<std::uint16_t>(value)); break;
    case PSMZ16S: put<std::uint16_t>(page<64, 64>(bp, bw, x, y) + g_offz16s[y & 63][x & 63], static_cast<std::uint16_t>(value)); break;
    case PSMT8: put<std::uint8_t>(page<128, 64>(bp, bw, x, y) + g_off8[y & 63][x & 127], static_cast<std::uint8_t>(value)); break;
    case PSMT4: {
        const std::uint32_t n = g_off4[y & 127][x & 127];
        const std::uint32_t a = page<128, 128>(bp, bw, x, y) + n / 2;
        std::uint8_t b = get<std::uint8_t>(a);
        b = (n & 1u) ? static_cast<std::uint8_t>((b & 0x0Fu) | ((value & 15u) << 4)) : static_cast<std::uint8_t>((b & 0xF0u) | (value & 15u));
        put<std::uint8_t>(a, b);
        break;
    }
    case PSMT8H: {
        const std::uint32_t a = page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63];
        put<std::uint32_t>(a, (get<std::uint32_t>(a) & 0x00FFFFFFu) | (value << 24));
        break;
    }
    case PSMT4HL: {
        const std::uint32_t a = page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63];
        put<std::uint32_t>(a, (get<std::uint32_t>(a) & 0xF0FFFFFFu) | ((value & 15u) << 24));
        break;
    }
    case PSMT4HH: {
        const std::uint32_t a = page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63];
        put<std::uint32_t>(a, (get<std::uint32_t>(a) & 0x0FFFFFFFu) | ((value & 15u) << 28));
        break;
    }
    default: put<std::uint32_t>(page<64, 32>(bp, bw, x, y) + g_off32[y & 31][x & 63], value); break;
    }
}

}
