#include "gs/gs.hpp"
#include "gs/raster.hpp"
#ifdef PS2_GPU
#include "gs/gpu/gpu.hpp"
#endif
#include "hw.hpp"
#include "ps2/system.hpp"

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

int failures = 0, checks = 0;

void expect(const char *name, std::uint64_t actual, std::uint64_t expected) {
    ++checks;
    if (actual == expected) return;
    ++failures;
    std::fprintf(stderr, "FAIL %s: got %llX, expected %llX\n", name,
                 static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
}

void advance_to(std::uint64_t at) {
    ps2::set_budget(ps2::g_cpu, static_cast<std::int64_t>(at - ps2::now()));
    ps2::g_cpu.budget = 0;
    ps2::preempt(ps2::g_cpu);
}

void timing(bool interlaced, unsigned mode) {
    using namespace ps2;
    struct Reference { bool pal, interlaced; unsigned mode, rate, lines, blank, gs; std::uint64_t hundred_thousand; };
    const Reference profiles[] = {
        {false, true, 2, 5994, 525, 421724, 65601, 492012012012ull},
        {true, true, 3, 5000, 625, 462422, 56623, 589824000000ull},
        {false, false, 2, 5983, 526, 431067, 74968, 492916597024ull},
        {true, false, 3, 4976, 628, 490744, 84936, 592668810289ull},
        {false, false, 0x50, 5994, 1050, 421724, 65601, 492012012012ull},
    };
    for (const auto &r : profiles) {
        const auto profile = video_timing_for(r.pal, r.interlaced, r.mode);
        std::uint64_t accumulated = 0;
        for (unsigned field = 0; field < 100000; ++field) accumulated += profile.field_cycles(kEeClock, field);
        expect("100,000 fields without fractional-cycle drift", accumulated, r.hundred_thousand);
        expect("mode blank duration", profile.half_lines_cycles(kEeClock, profile.blank_half_lines), r.blank);
        expect("mode GS delay", profile.half_lines_cycles(kEeClock, profile.gs_half_lines), r.gs);
        for (unsigned lines : {1u, 5u, 65535u}) {
            const auto at = profile.cycles_for_scanlines(kEeClock, lines);
            expect("scanline before edge", profile.scanlines_elapsed(kEeClock, at - 1), lines - 1);
            expect("scanline edge", profile.scanlines_elapsed(kEeClock, at), lines);
        }
    }
    const Reference &r = profiles[mode == 0x50 ? 4 : g_options.pal ? interlaced ? 1 : 3 : interlaced ? 0 : 2];
    boot(0x100000, {});
    std::uint64_t expected_deadline = kEeClock * 100 / (g_options.pal ? 5000 : 5994);
    hw::gs_set_crt(interlaced, mode, 1);
    expect("SetGsCrt sets SMODE2", gs::display_regs().smode2 & 3, interlaced ? 3 : 2);
    for (unsigned frame = 1; frame <= 8; ++frame) {
        const std::uint64_t deadline = scheduled_at(hw::kEventVblank);
        expect("vblank deadline", deadline, expected_deadline);
        hw::write32(0x1000F000, 8);
        gs::write_privileged(0x12001000, 8);
        const bool previous_field = gs::interlaced_odd_field();
        // Model an EE block finishing 137 cycles beyond the event budget.
        advance_to(deadline + 137);
        expect("one field per deadline", frames_run(), frame);
        expected_deadline += kEeClock * 100 * (frame + 1) / r.rate - kEeClock * 100 * frame / r.rate;
        expect("no accumulated vblank drift", scheduled_at(hw::kEventVblank), expected_deadline);
        expect("EE vblank end scheduled from deadline", scheduled_at(hw::kEventVblankEnd), deadline + r.blank);
        expect("IOP vblank end agrees", scheduled_at(hw::kEventIopVblankEnd), deadline + r.blank);
        expect("GS blank is delayed", gs::read_privileged(0x12001000) & 8, 0);
        expect("FIELD has not changed at EE blank start", gs::interlaced_odd_field(), previous_field);
        if (frame == 4) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto path = std::filesystem::temp_directory_path() / ("ps2-accuracy-" + std::to_string(stamp) + ".ps2");
            const bool saved = save_state(path.string());
            expect("save synthetic timing state", saved, true);
            if (saved) {
                advance_to(deadline + r.blank);
                expect("reload synthetic timing state", load_state(path.string()), true);
                expect("reload restores pre-GS edge", gs::read_privileged(0x12001000) & 8, 0);
                expect("reload clears future blank-end latch", hw::read32(0x1000F000) & 8, 0);
            }
            std::filesystem::remove(path);
        }
        advance_to(deadline + r.gs - 1);
        expect("GS blank before edge", gs::read_privileged(0x12001000) & 8, 0);
        advance_to(deadline + r.gs);
        expect("GS blank at edge", gs::read_privileged(0x12001000) & 8, 8);
        expect("FIELD follows interlace mode", gs::interlaced_odd_field(), interlaced ? !previous_field : false);
        advance_to(deadline + r.blank - 1);
        expect("EE blank end before edge", hw::read32(0x1000F000) & 8, 0);
        advance_to(deadline + r.blank);
        expect("EE blank end at edge", hw::read32(0x1000F000) & 8, 8);
    }
    // EE timer 0: HBlank clock, target interrupt after five rational scanlines.
    hw::write32(0x10000020, 5);
    hw::write32(0x10000000, 0);
    hw::write32(0x10000010, 0x183);
    const auto target = scheduled_at(hw::kEventTimer0);
    expect("HBlank timer target", target, now() + (5 * kEeClock * 200 + r.rate * r.lines - 1) / (r.rate * r.lines));
    advance_to(target - 1);
    expect("HBlank counter before target", hw::read32(0x10000000), 4);
    advance_to(target);
    expect("HBlank timer IRQ", hw::read32(0x1000F000) & (1u << 9), 1u << 9);
    const auto counter = hw::read32(0x10000000);
    const auto pending_field = scheduled_at(hw::kEventVblank);
    hw::gs_set_crt(1, r.pal ? 2 : 3, 1);
    expect("mode switch preserves HBlank count", hw::read32(0x10000000), counter);
    expect("mode switch preserves pending field boundary", scheduled_at(hw::kEventVblank), pending_field);
    advance_to(pending_field + 137);
    const unsigned new_rate = r.pal ? 5994 : 5000;
    expect("mode switch changes next field interval", scheduled_at(hw::kEventVblank),
           pending_field + kEeClock * 100 * 10 / new_rate - kEeClock * 100 * 9 / new_rate);
    g_options.rate_forced = true;
    hw::gs_set_crt(1, g_options.pal ? 2 : 3, 1);
    expect("forced region takes precedence", video_timing().rate, new_rate);
    expect("unsupported mode is explicit", video_timing_for(false, true, 0x52).supported, false);
    hw::write32(0x10000010, 0);
    hw::write32(0x10000020, 20);
    hw::write32(0x10000000, 0);
    hw::write32(0x10000010, 0x183);
    const auto legacy_line = (kEeClock / (g_options.pal ? 50u : 60u)) * 2 / (g_options.pal ? 625u : 525u);
    advance_to(now() + 10 * legacy_line);
    hw::retime_legacy_video();
    expect("legacy HBlank count migrated", hw::read32(0x10000000), 10);
    expect("legacy HBlank target rescheduled", scheduled_at(hw::kEventTimer0),
           now() - video_timing().cycles_for_scanlines(kEeClock, 10) + video_timing().cycles_for_scanlines(kEeClock, 20));
    for (unsigned version : {2u, 3u, 4u}) {
        expect("legacy IOP vblank event", hw::restored_event_id(101, version), hw::kEventIopVblankEnd);
        expect("legacy first alarm", hw::restored_event_id(18, version), hw::kEventCount + 1);
        expect("legacy hardware event", hw::restored_event_id(hw::kEventTimer0, version), hw::kEventTimer0);
    }
    expect("current GS vblank event", hw::restored_event_id(hw::kEventGsVblank, 5), hw::kEventGsVblank);
}

void destination_alpha(bool gpu) {
    using namespace ps2::gs;
    init();
#ifdef PS2_GPU
    if (gpu && ps2::gs::gpu::mode() == ps2::gs::gpu::Mode::off) {
        std::fprintf(stderr, "FAIL GPU requested but unavailable\n");
        ++failures;
        return;
    }
#else
    if (gpu) {
        std::fprintf(stderr, "FAIL GPU support was not built\n");
        ++failures;
        return;
    }
#endif
    for (const auto format : {PSMCT32, PSMCT24, PSMCT16, PSMCT16S}) {
        for (unsigned alpha = 0; alpha < 2; ++alpha) {
            for (unsigned datm = 0; datm < 2; ++datm) {
                std::memset(memory(), 0, kMemorySize);
                const bool narrow = format == PSMCT16 || format == PSMCT16S;
                const std::uint32_t initial = narrow ? 0x1234u | (alpha << 15) : 0x00123456u | (alpha << 31);
                // Read PSMCT24 as PSMCT32 to check preservation of the unused byte.
                const auto storage = format == PSMCT24 ? PSMCT32 : format;
                write_pixel(storage, 0, 1, 2, 2, initial);
                State state{};
                state.prmodecont = 1;
                state.colclamp = 1;
                state.ctx[0].frame = (1ull << 16) | (static_cast<std::uint64_t>(format) << 24);
                state.ctx[0].zbuf = 16; // Separate framebuffer/depth pages.
                state.ctx[0].scissor = (7ull << 16) | (7ull << 48);
                state.ctx[0].test = (1u << 14) | (datm << 15) | (1u << 16) | (1u << 17);
                Vertex vertex{};
                vertex.x = vertex.y = 2 * 16;
                vertex.z = 0x12345678;
                vertex.r = 0xF8;
                vertex.g = 0x80;
                vertex.b = 0x40;
                vertex.a = 0x80;
#ifdef PS2_GPU
                if (gpu) {
                    std::vector<std::uint32_t> vram(kMemorySize / 4);
                    std::memcpy(vram.data(), memory(), kMemorySize);
                    ps2::gs::gpu::write_state(vram, 1, {});
                    ++g_setup_generation;
                    ps2::gs::gpu::draw(state, &vertex, 1);
                    ps2::gs::gpu::download(memory());
                } else
#endif
                raster::draw(state, &vertex, 1);
                const bool pass = format == PSMCT24 || alpha == datm;
                const std::uint32_t color = narrow ? 0xA21Fu : format == PSMCT24 ? (initial & 0xFF000000u) | 0x004080F8u : 0x804080F8u;
                char label[100];
                std::snprintf(label, sizeof label, "%s DATE psm=%X alpha=%u datm=%u", gpu ? "GPU" : "software", format, alpha, datm);
                expect(label, read_pixel(storage, 0, 1, 2, 2), pass ? color : initial);
                expect("DATE gates depth too", read_pixel(PSMZ32, 16 * 32, 1, 2, 2), pass ? vertex.z : 0);
            }
        }
    }
}

void dithering(bool gpu) {
    using namespace ps2::gs;
    constexpr int matrix[16] = {-4, -1, 2, 0, 3, -2, 1, -3, 1, 3, -4, 2, -1, 0, -3, -2};
    std::uint64_t dimx = 0;
    for (unsigned i = 0; i < 16; ++i)
        dimx |= static_cast<std::uint64_t>((matrix[i] & 7) | 8) << (i * 4); // Reserved slot bits must be ignored.
    for (const auto format : {PSMCT32, PSMCT24, PSMCT16, PSMCT16S}) {
        const bool narrow = format == PSMCT16 || format == PSMCT16S;
        const auto storage = format == PSMCT24 ? PSMCT32 : format;
        const std::uint32_t initial = narrow ? 0x8C63u : 0x7F181818u;
        for (unsigned enabled = 0; enabled < 2; ++enabled)
            for (unsigned clamp = 0; clamp < 2; ++clamp)
                for (unsigned blend = 0; blend < 3; ++blend)
                    for (unsigned masked = 0; masked < 2; ++masked) {
                        std::memset(memory(), 0, kMemorySize);
                        State state{};
                        state.prmodecont = 1;
                        state.prim = blend ? 0x40 : 0;
                        state.colclamp = clamp;
                        state.dthe = enabled;
                        state.dimx = dimx;
                        // Preserve part of green, and use AFAIL RGB_ONLY to preserve alpha.
                        const std::uint32_t mask = masked ? 0x0000F800u : 0;
                        state.ctx[0].frame = (1ull << 16) | (static_cast<std::uint64_t>(format) << 24) |
                                             (static_cast<std::uint64_t>(mask) << 32);
                        state.ctx[0].scissor = (7ull << 16) | (7ull << 48);
                        state.ctx[0].zbuf = 1ull << 32;
                        state.ctx[0].test = masked ? 1 | (3 << 12) : 0;
                        // Blend with FIX=128: source + destination, or destination - source.
                        state.ctx[0].alpha = (128ull << 32) | (blend == 1 ? 0x68 : 0xA1);
                        Vertex vertices[64]{};
                        for (unsigned y = 0; y < 8; ++y)
                            for (unsigned x = 0; x < 8; ++x) {
                                write_pixel(storage, 0, 1, x, y, initial);
                                Vertex &v = vertices[y * 8 + x];
                                v.x = x * 16;
                                v.y = y * 16;
                                v.r = (x & 3) * 85;
                                v.g = (y & 3) * 85;
                                v.b = 7 + ((x + y) & 3) * 8;
                                v.a = (y & 1) ? 0x80 : 0x7F;
                            }
#ifdef PS2_GPU
                        if (gpu) {
                            std::vector<std::uint32_t> vram(kMemorySize / 4);
                            std::memcpy(vram.data(), memory(), kMemorySize);
                            ps2::gs::gpu::write_state(vram, 1, {});
                            ++g_setup_generation;
                            for (const auto &v : vertices) ps2::gs::gpu::draw(state, &v, 1);
                            ps2::gs::gpu::download(memory());
                        } else
#endif
                        for (const auto &v : vertices) raster::draw(state, &v, 1);
                        for (unsigned y = 0; y < 8; ++y)
                            for (unsigned x = 0; x < 8; ++x) {
                                const Vertex &v = vertices[y * 8 + x];
                                const int offset = narrow && enabled ? matrix[(y & 3) * 4 + (x & 3)] : 0;
                                const auto channel = [&](int source) {
                                    int value = blend == 1 ? source + 24 : blend == 2 ? 24 - source : source;
                                    value += offset;
                                    return static_cast<std::uint32_t>(clamp ? std::clamp(value, 0, 255) : (value & 255));
                                };
                                const auto r = channel(v.r), g = channel(v.g), b = channel(v.b);
                                std::uint32_t expected = narrow ? (r / 8) | ((g / 8) << 5) | ((b / 8) << 10) | (v.a >= 128 ? 0x8000u : 0) :
                                                                         r | (g << 8) | (b << 16) | (static_cast<std::uint32_t>(v.a) << 24);
                                const std::uint32_t keep = narrow ? masked ? 0x83E0u : 0 :
                                                                   mask | (masked || format == PSMCT24 ? 0xFF000000u : 0);
                                expected = (expected & ~keep) | (initial & keep);
                                char label[150];
                                std::snprintf(label, sizeof label, "%s dither psm=%X on=%u clamp=%u blend=%u mask=%u pixel=%u,%u",
                                              gpu ? "GPU" : "software", format, enabled, clamp, blend, masked, x, y);
                                expect(label, read_pixel(storage, 0, 1, x, y), expected);
                            }
                    }
    }
}

void dither_register_changes(bool gpu) {
    using namespace ps2::gs;
    reset();
#ifdef PS2_GPU
    if (gpu) ps2::gs::gpu::write_state(std::vector<std::uint32_t>(kMemorySize / 4), 1, {});
#endif
    write(0x1A, 1);
    write(0x00, 0);
    write(0x46, 1);
    write(0x4C, (1ull << 16) | (static_cast<std::uint64_t>(PSMCT16) << 24));
    write(0x4E, 1ull << 32);
    write(0x40, (7ull << 16) | (7ull << 48));
    write(0x01, 0x80070707);
    write(0x45, 1);
    write(0x44, 0x1111111111111111ull);
    write(0x05, 0); // +1 brings all three channels across a 5-bit boundary.
    write(0x44, 0x7777777777777777ull);
    write(0x05, 16); // -1, within the same GPU batch but a different setup.
    write(0x44, 0x1111111111111111ull);
    write(0x45, 0);
    write(0x05, 32); // DTHE off.
    write(0x45, 1);
    write(0x05, 48);
#ifdef PS2_GPU
    if (gpu) ps2::gs::gpu::download(memory());
#endif
    expect("DIMX +1 through GS registers", read_pixel(PSMCT16, 0, 1, 0, 0), 0x8421);
    expect("DIMX -1 changes cached setup", read_pixel(PSMCT16, 0, 1, 1, 0), 0x8000);
    expect("DTHE off changes cached setup", read_pixel(PSMCT16, 0, 1, 2, 0), 0x8000);
    expect("DTHE on changes cached setup", read_pixel(PSMCT16, 0, 1, 3, 0), 0x8421);
}

void mipmapping(bool gpu) {
    using namespace ps2::gs;
    const std::uint32_t colors[] = {0x80402010u, 0x80503020u, 0x80604030u, 0x80705040u,
                                    0x80806050u, 0x80907060u, 0x80A08070u};
    std::memset(memory(), 0, kMemorySize);
    State state{};
    state.prmodecont = 1;
    state.prim = 0x10;
    state.colclamp = 1;
    auto &c = state.ctx[0];
    c.frame = 1ull << 16;
    c.scissor = (63ull << 16) | (63ull << 48);
    c.zbuf = 1ull << 32;
    c.tex0 = 64 | (1ull << 14) | (3ull << 26) | (3ull << 30) | (1ull << 34) | (1ull << 35);
    for (unsigned n = 0; n <= 6; ++n) {
        const unsigned bp = 64 + n * 64;
        if (n) {
            auto &reg = n <= 3 ? c.miptbp1 : c.miptbp2;
            reg |= static_cast<std::uint64_t>(bp | (1u << 14)) << (((n - 1) % 3) * 20);
        }
        const unsigned size = std::max(1u, 8u >> n);
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) write_pixel(PSMCT32, bp, 1, x, y, colors[n]);
    }
#ifdef PS2_GPU
    if (gpu) {
        std::vector<std::uint32_t> vram(kMemorySize / 4);
        std::memcpy(vram.data(), memory(), kMemorySize);
        ps2::gs::gpu::write_state(vram, 1, {});
    }
#endif
    unsigned pixel = 0;
    for (unsigned mmin = 0; mmin <= 5; ++mmin)
        for (bool fixed : {false, true})
            for (int bias : {-16, 0, 4, 8, 12, 16, 24, 88, 96, 112}) {
                // Dynamic Q gives an additional +1 level; constant mode ignores it.
                c.tex1 = (6u << 2) | (mmin << 6) | (fixed ? 1u : 0u) |
                         (static_cast<std::uint64_t>(bias & 0xFFF) << 32);
                Vertex v{};
                v.x = (pixel % 32) * 16;
                v.y = (pixel / 32) * 16;
                v.q = 0.5f;
                v.s = v.t = 0.25f;
#ifdef PS2_GPU
                if (gpu) { ++g_setup_generation; ps2::gs::gpu::draw(state, &v, 1); }
                else
#endif
                raster::draw(state, &v, 1);
                ++pixel;
            }
#ifdef PS2_GPU
    if (gpu) ps2::gs::gpu::download(memory());
#endif
    pixel = 0;
    for (unsigned mmin = 0; mmin <= 5; ++mmin)
        for (bool fixed : {false, true})
            for (int bias : {-16, 0, 4, 8, 12, 16, 24, 88, 96, 112}) {
                const float lod = std::clamp(bias / 16.0f + (fixed ? 0.0f : 1.0f), 0.0f, 6.0f);
                const bool tri = mmin >= 2 && (mmin & 1);
                const unsigned first = mmin < 2 ? 0 : static_cast<unsigned>(lod + (tri ? 0.0f : 0.5f));
                const unsigned next = std::min(first + 1, 6u);
                const float fraction = tri ? lod - first : 0.0f;
                std::uint32_t expected = 0;
                for (unsigned ch = 0; ch < 32; ch += 8) {
                    const float a = (colors[first] >> ch) & 255u, b = (colors[next] >> ch) & 255u;
                    expected |= static_cast<std::uint32_t>(a + (b - a) * fraction + 0.5f) << ch;
                }
                expect("mip levels, signed bias, dynamic Q, minification and trilinear", read_pixel(PSMCT32, 0, 1, pixel % 32, pixel / 32), expected);
                ++pixel;
            }
    // A horizontal ramp distinguishes point and bilinear filtering at each level.
    for (unsigned n = 0; n <= 1; ++n)
        for (unsigned y = 0; y < (8u >> n); ++y)
            for (unsigned x = 0; x < (8u >> n); ++x)
                write_pixel(PSMCT32, 64 + n * 64, 1, x, y, 0x80000000u | (x * 32));
#ifdef PS2_GPU
    if (gpu) {
        std::vector<std::uint32_t> vram(kMemorySize / 4);
        std::memcpy(vram.data(), memory(), kMemorySize);
        ps2::gs::gpu::write_state(vram, 1, {});
    }
#endif
    const unsigned ramp[] = {128, 112, 64, 64, 48, 48};
    for (unsigned filter = 0; filter < 6; ++filter) {
        c.tex1 = (1u << 2) | (filter << 6);
        Vertex v{};
        v.x = filter * 16;
        v.y = 16 * 10;
        v.q = 0.5f; v.s = v.t = 0.25f;
#ifdef PS2_GPU
        if (gpu) { ++g_setup_generation; ps2::gs::gpu::draw(state, &v, 1); }
        else
#endif
        raster::draw(state, &v, 1);
    }
#ifdef PS2_GPU
    if (gpu) ps2::gs::gpu::download(memory());
#endif
    for (unsigned filter = 0; filter < 6; ++filter)
        expect("mip coordinate scaling and minification filtering", read_pixel(PSMCT32, 0, 1, filter, 10), 0x80000000u | ramp[filter]);
}

}

void constant_triangle_alpha(bool gpu) {
    using namespace ps2::gs;
    std::memset(memory(), 0, kMemorySize);
    State state{};
    state.prmodecont = 1;
    state.prim = 3 | 8;
    state.colclamp = 1;
    state.ctx[0].frame = 1ull << 16;
    state.ctx[0].zbuf = 16;
    state.ctx[0].scissor = (63ull << 16) | (63ull << 48);
    state.ctx[0].test = 1 | (5 << 1) | (128 << 4) | (1 << 16) | (2 << 17);
    Vertex v[3]{};
    v[0].x = 49; v[0].y = 35;
    v[1].x = 989; v[1].y = 81;
    v[2].x = 101; v[2].y = 959;
    for (unsigned i = 0; i < 3; ++i) { v[i].r = v[i].a = 128; v[i].z = 0xFFFF0000u + i * 97; }
#ifdef PS2_GPU
    if (gpu) {
        ps2::gs::gpu::write_state(std::vector<std::uint32_t>(kMemorySize / 4), 1, {});
        ++g_setup_generation;
        ps2::gs::gpu::draw(state, v, 3);
        std::swap(v[0], v[2]);
        for (auto &p : v) { p.r = 0; p.g = 128; }
        ps2::gs::gpu::draw(state, v, 3);
        ps2::gs::gpu::download(memory());
    } else
#endif
    {
        raster::draw(state, v, 3);
        std::swap(v[0], v[2]);
        for (auto &p : v) { p.r = 0; p.g = 128; }
        raster::draw(state, v, 3);
    }
    unsigned rejected = 0;
    for (unsigned y = 8; y < 24; ++y)
        for (unsigned x = 8; x < 24; ++x)
            rejected += read_pixel(PSMCT32, 0, 1, x, y) != 0x80008000u;
    expect("constant alpha and reordered equal-depth passes have no speckles", rejected, 0);
#ifdef PS2_GPU
    if (gpu) {
        std::vector<std::uint32_t> vram, clut;
        std::uint32_t scale;
        ps2::gs::gpu::read_state(vram, scale, clut);
        unsigned rejected_layers = 0;
        for (unsigned y = 8; y < 24; ++y)
            for (unsigned x = 8; x < 24; ++x) {
                const unsigned word = address32(0, 1, x, y) / 4;
                for (unsigned k = 0; k < scale * scale; ++k)
                    rejected_layers += vram[word * scale * scale + k] != 0x80008000u;
            }
        expect("all supersampled layers preserve alpha and coplanar pass depth", rejected_layers, 0);
    }
#endif
}

int main(int argc, char **argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "--soft";
    if (mode.starts_with("--timing-")) {
        ps2::g_options.pal = mode == "--timing-pal" || mode == "--timing-pal-ni";
        const bool interlaced = mode != "--timing-pal-ni" && mode != "--timing-ntsc-ni" && mode != "--timing-480p";
        timing(interlaced, mode == "--timing-480p" ? 0x50 : ps2::g_options.pal ? 3 : 2);
    } else if (mode == "--soft" || mode == "--gpu") {
        destination_alpha(mode == "--gpu");
        if (!failures) dithering(mode == "--gpu");
        if (!failures) dither_register_changes(mode == "--gpu");
        if (!failures) mipmapping(mode == "--gpu");
        if (!failures) constant_triangle_alpha(mode == "--gpu");
    } else {
        std::fprintf(stderr, "usage: ps2_accuracy [--soft|--gpu|--timing-pal|--timing-ntsc]\n");
        return 2;
    }
    std::printf("%d/%d accuracy checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
