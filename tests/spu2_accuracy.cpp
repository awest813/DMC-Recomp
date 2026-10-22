#include "iop/spu2.hpp"
#include "iop/spu2_gaussian.hpp"
#include "iop/iop.hpp"
#include "ps2/state.hpp"

#include <array>
#include <cstdio>
#include <vector>

namespace {

int failures = 0, checks = 0;

void expect(const char *name, std::int64_t got, std::int64_t expected) {
    ++checks;
    if (got == expected) return;
    if (++failures < 15) std::fprintf(stderr, "FAIL %s: got %lld expected %lld\n", name,
                                    static_cast<long long>(got), static_cast<long long>(expected));
}

void filter_tests() {
    using namespace ps2::iop::spu2;
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto coefficient : kGaussian)
        for (unsigned shift : {0u, 8u}) hash = (hash ^ ((coefficient >> shift) & 255)) * 1099511628211ull;
    expect("published ROM coefficient hash", static_cast<std::int64_t>(hash), 0x229D8F8355032A87ull);
    const std::int16_t taps[4] = {-12345, 23456, -30001, 32767};
    const unsigned phases[] = {0, 1, 63, 127, 128, 191, 254, 255};
    const int expected[] = {10165, 10128, 5414, -2851, -2986, -10349, -12749, -12732};
    for (unsigned i = 0; i < 8; ++i) expect("published phase vector", gaussian(taps, phases[i] << 4), expected[i]);
    for (unsigned phase = 0; phase < 256; ++phase) {
        const std::int16_t zero[4] = {};
        expect("silent input", gaussian(zero, phase << 4), 0);
        for (unsigned low = 1; low < 16; ++low)
            expect("fractional bits 0-3 ignored", gaussian(taps, (phase << 4) | low), gaussian(taps, phase << 4));
    }
    const std::int16_t small[4] = {1, 1, 1, 1};
    expect("truncate each product before sum", gaussian(small, 0), -1);
}

std::vector<std::int16_t> captured;

int source_sample(int index) {
    if (index < 0) return 0;
    index %= 112;
    int nibble = (index * 7 + index / 3) & 15;
    return (nibble < 8 ? nibble : nibble - 16) * 1024;
}

void configure(unsigned core, unsigned pitch) {
    using namespace ps2::iop;
    reset();
    captured.clear();
    spu2::set_sink([](const std::int16_t *samples, std::size_t frames) {
        captured.insert(captured.end(), samples, samples + frames * 2);
    });
    constexpr unsigned base = 0x10000;
    auto *ram = spu2::ram();
    for (unsigned block = 0; block < 4; ++block) {
        ram[base + block * 8] = 2 | ((block == 0 ? 4 : block == 3 ? 3 : 0) << 8);
        for (unsigned word = 0; word < 7; ++word) {
            unsigned packed = 0;
            for (unsigned nibble = 0; nibble < 4; ++nibble) {
                const int index = block * 28 + word * 4 + nibble;
                packed |= ((index * 7 + index / 3) & 15) << (nibble * 4);
            }
            ram[base + block * 8 + 1 + word] = packed;
        }
    }
    const auto reg = [core](unsigned offset, unsigned value) { spu2::write(core * 0x400 + offset, value, 2); };
    reg(0x000, 0x3FFF);
    reg(0x002, 0x2000);
    reg(0x004, pitch);
    reg(0x006, 0x00FF); // Fast attack, then sustain at full envelope level.
    reg(0x008, 0x1FC0);
    reg(0x1C0, base >> 16);
    reg(0x1C2, base & 65535);
    reg(0x1A0, 1);
}

void advance(unsigned frames) { ps2::iop::run(static_cast<std::uint64_t>(frames) * 768 * 8 + 8); }

void voice_tests() {
    using namespace ps2::iop;
    for (unsigned core : {0u, 1u})
        for (unsigned pitch : {0u, 0x800u, 0x1000u, 0x3800u, 0x3FFFu}) {
            configure(core, pitch);
            advance(256);
            expect("48kHz sample count", captured.size(), 512);
            if (captured.size() != 512) continue;
            for (unsigned n = 0; n < 256; ++n) {
                const auto position = n * pitch;
                const int index = position >> 12;
                std::int16_t taps[4];
                for (int tap = 0; tap < 4; ++tap) taps[tap] = source_sample(index + tap - 3);
                const int envelope = n == 0 ? 14336 : n == 1 ? 28672 : 32767;
                const auto sample = (spu2::gaussian(taps, position & 4095) * envelope) >> 15;
                const int left = ((sample * 32766) >> 15) * 32766 >> 15;
                const int right = ((sample * 16384) >> 15) * 32766 >> 15;
                char label[90];
                std::snprintf(label, sizeof label, "core %u pitch %04X sample %u left", core, pitch, n);
                expect(label, captured[n * 2], left);
                expect("right channel volume", captured[n * 2 + 1], right);
            }
        }
    configure(0, 0x3800);
    advance(32); // Exactly on a block/loop boundary: the next sample needs saved history.
    ps2::state::Writer writer;
    writer.set_version(4);
    state(writer);
    captured.clear();
    advance(32);
    const auto uninterrupted = captured;
    spu2::reset(); // Clear history to prove it is restored, not left over in process memory.
    ps2::state::Reader reader(writer.out.data(), writer.out.size());
    reader.set_version(4);
    state(reader);
    captured.clear();
    advance(32);
    expect("save/load sample count", captured.size(), uninterrupted.size());
    for (unsigned i = 0; i < captured.size() && i < uninterrupted.size(); ++i)
        expect("save/load preserves filter history", captured[i], uninterrupted[i]);
    // Older states carry no interpolation history. Loading must initialize it deterministically.
    ps2::state::Writer legacy;
    legacy.set_version(3);
    state(legacy);
    const auto load_legacy = [&] {
        ps2::state::Reader old(legacy.out.data(), legacy.out.size());
        old.set_version(3);
        state(old);
    };
    load_legacy();
    captured.clear();
    advance(32);
    const auto first_legacy = captured;
    load_legacy();
    captured.clear();
    advance(32);
    expect("legacy state sample count", captured.size(), first_legacy.size());
    for (unsigned i = 0; i < captured.size() && i < first_legacy.size(); ++i)
        expect("legacy state history is deterministic", captured[i], first_legacy[i]);
    spu2::write(0x1A0, 1, 2);
    captured.clear();
    advance(32);
    const auto restarted = captured;
    configure(0, 0x3800);
    advance(32);
    expect("key-on sample count", captured.size(), restarted.size());
    for (unsigned i = 0; i < captured.size() && i < restarted.size(); ++i)
        expect("key-on clears old filter history", captured[i], restarted[i]);
    spu2::set_sink({});
}

void pcm_input_tests() {
    using namespace ps2::iop;
    for (unsigned core : {0u, 1u}) {
        reset();
        captured.clear();
        spu2::set_sink([](const std::int16_t *samples, std::size_t frames) {
            captured.insert(captured.end(), samples, samples + frames * 2);
        });
        std::int16_t inputs[2][256];
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < 256; ++n) {
                inputs[channel][n] = static_cast<std::int16_t>(n * 251 + channel * 97 - 32768);
                std::memcpy(ram_ptr(0x1000 + channel * 512 + n * 2), &inputs[channel][n], 2);
            }
        spu2::write(core * 0x400 + 0x1B0, 1u << core, 2);
        spu2::dma(core, 0x1000, 256, true, -1);
        advance(256);
        expect("PCM input sample count", captured.size(), 512);
        for (unsigned i = 0; i < captured.size() && i < 512; ++i) {
            const auto sample = inputs[i % 2][i / 2];
            const auto expected = ((sample * 32767) >> 15) * 32766 >> 15;
            expect("PCM input bypasses Gaussian filter", captured[i], expected);
        }
    }
    spu2::set_sink({});
}

void routing_tests() {
    using namespace ps2::iop;
    for (unsigned core : {0u, 1u}) {
        configure(core, 0x1000);
        advance(32);
        const auto reference = captured;
        std::int16_t buses[2][32];
        for (unsigned ch = 0; ch < 2; ++ch)
            for (unsigned n = 0; n < 32; ++n)
                buses[ch][n] = static_cast<std::int16_t>(spu2::ram()[0x1000 + core * 0x800 + ch * 0x200 + 32 + n]);
        for (unsigned voice : {0u, 15u, 16u, 23u})
            for (unsigned gates = 0; gates < 16; ++gates) {
                configure(core, 0x1000);
                const auto write = [&](unsigned reg, unsigned value) { spu2::write(core * 0x400 + reg, value, 2); };
                if (voice) {
                    write(0x1A4, 1); // Cancel the pending key-on for voice zero.
                    for (unsigned reg : {0u, 2u, 4u, 6u, 8u})
                        write(voice * 16 + reg, spu2::read(core * 0x400 + reg, 2));
                    write(0x1C0 + voice * 12, 1);
                    write(0x1C2 + voice * 12, 0);
                    write(voice < 16 ? 0x1A0 : 0x1A2, 1u << (voice & 15));
                }
                const unsigned regs[] = {0x188, 0x190, 0x18C, 0x194};
                for (unsigned bus = 0; bus < 4; ++bus) {
                    write(regs[bus], 0);
                    write(regs[bus] + 2, 0);
                    if (gates & (1u << bus)) write(regs[bus] + (voice >= 16 ? 2 : 0), 1u << (voice & 15));
                }
                advance(32);
                expect("routing sample count", captured.size(), 64);
                for (unsigned n = 0; n < 32; ++n) {
                    for (unsigned ch = 0; ch < 2; ++ch) {
                        expect("independent dry L/R voice gates", captured[n * 2 + ch], gates & (1u << ch) ? reference[n * 2 + ch] : 0);
                        for (unsigned wet = 0; wet < 2; ++wet) {
                            const auto at = 0x1000 + core * 0x800 + (wet * 2 + ch) * 0x200 + 32 + n;
                            const auto actual = static_cast<std::int16_t>(spu2::ram()[at]);
                            expect("independent dry/wet capture buses", actual, gates & (1u << (wet * 2 + ch)) ? buses[ch][n] : 0);
                        }
                    }
                }
            }
        // Muting must not freeze decoding/envelopes. Re-enable after one complete batch.
        configure(core, 0x1000);
        advance(64);
        const auto uninterrupted = captured;
        configure(core, 0x1000);
        spu2::write(core * 0x400 + 0x188, 0, 2);
        spu2::write(core * 0x400 + 0x190, 0, 2);
        advance(32);
        spu2::write(core * 0x400 + 0x188, 1, 2);
        spu2::write(core * 0x400 + 0x190, 1, 2);
        advance(32);
        for (unsigned n = 64; n < captured.size(); ++n)
            expect("muted voice keeps advancing", captured[n], uninterrupted[n]);
        configure(core, 0x1000);
        spu2::write(core * 0x400 + 0x188, 0, 2);
        advance(32);
        ps2::state::Writer saved;
        saved.set_version(5);
        state(saved);
        captured.clear();
        advance(32);
        const auto routed_reference = captured;
        reset();
        ps2::state::Reader restored(saved.out.data(), saved.out.size());
        restored.set_version(5);
        state(restored);
        captured.clear();
        advance(32);
        expect("save/load restores routing register", spu2::read(core * 0x400 + 0x188, 2), 0);
        for (unsigned n = 0; n < captured.size(); ++n)
            expect("save/load preserves routed samples", captured[n], routed_reference[n]);
        // Wet-ring writes must still raise IRQA when the voice is inaudible on the dry bus.
        configure(core, 0x1000);
        for (unsigned reg : {0x188u, 0x190u}) spu2::write(core * 0x400 + reg, 0, 2);
        const unsigned wet_address = 0x1400 + core * 0x800 + 32;
        spu2::write(core * 0x400 + 0x19C, 0, 2);
        spu2::write(core * 0x400 + 0x19E, wet_address, 2);
        spu2::write(core * 0x400 + 0x19A, 0x40, 2);
        advance(32);
        expect("wet capture trips IRQ", spu2::read(0x7C2, 2) & (4u << core), 4u << core);
        expect("wet-only voice is not sent directly to output", captured[40], 0);
        expect("wet-only voice writes nonzero capture", spu2::ram()[wet_address + 20] != 0, true);
        configure(core, 0x1000);
        for (unsigned block = 0; block < 4; ++block) {
            spu2::ram()[0x10000 + block * 8] = (block == 0 ? 4 : block == 3 ? 3 : 0) << 8;
            for (unsigned word = 1; word < 8; ++word) spu2::ram()[0x10000 + block * 8 + word] = 0x7777;
        }
        for (unsigned reg : {0u, 2u, 4u, 6u, 8u})
            spu2::write(core * 0x400 + 16 + reg, spu2::read(core * 0x400 + reg, 2), 2);
        spu2::write(core * 0x400 + 0x1CC, 1, 2);
        spu2::write(core * 0x400 + 0x1CE, 0, 2);
        spu2::write(core * 0x400 + 0x1A0, 3, 2);
        advance(32);
        expect("dry voice bus saturates before master gain", captured[40], (32767 * 32766) >> 15);
        expect("wet voice bus saturates independently", static_cast<std::int16_t>(spu2::ram()[0x1400 + core * 0x800 + 52]), 32767);
        const auto raw_voice = static_cast<std::int16_t>(spu2::ram()[0x400 + core * 0x800 + 52]);
        expect("raw voice capture is independent of bus saturation", raw_voice > 0 && raw_voice < 32767, true);
    }
    spu2::set_sink({});
}

}

int main() {
    filter_tests();
    voice_tests();
    pcm_input_tests();
    routing_tests();
    std::printf("%d/%d SPU2 accuracy checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
