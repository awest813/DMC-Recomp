// Replays VU1 calls saved with PS2_VU_CAPTURE through the AOT-compiled programs and
// reports the time per pass and a checksum of the results (registers, VU memory and
// XGKICK packets), so code generator changes can be timed and checked for identical
// output. Build it with the generated vu1_*.cpp files of a game, natively or with emcc.
// usage: vubench <capture file> [passes]
#include "ps2/vu_ops.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

void register_generated_vu();

namespace {

struct Call {
    std::uint64_t key;
    std::uint32_t pc, top, itop;
    ps2::Vu0Regs regs;
    std::vector<std::uint8_t> mem;
};

std::unordered_map<std::uint64_t, ps2::vu::ProgramFn> g_programs;
const Call *g_call = nullptr;
std::uint8_t *g_mem = nullptr;
std::uint64_t g_hash = 0xCBF29CE484222325ull;
std::uint64_t g_escapes = 0, g_kicked = 0;

void mix(const void *data, std::size_t n) {
    const auto *p = static_cast<const std::uint8_t *>(data);
    for (std::size_t i = 0; i + 8 <= n; i += 8) {
        std::uint64_t v;
        std::memcpy(&v, p + i, 8);
        g_hash = (g_hash ^ v) * 0x100000001B3ull;
    }
    for (std::size_t i = n & ~std::size_t{7}; i < n; ++i) g_hash = (g_hash ^ p[i]) * 0x100000001B3ull;
}

}

namespace ps2::vu {

void register_programs(const Program *programs, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i)
        if (programs[i].function) g_programs[programs[i].key] = programs[i].function;
}

std::uint32_t top() { return g_call->top; }
std::uint32_t itop() { return g_call->itop; }
void interpret(int, std::uint32_t pc) {
    ++g_escapes;
    mix(&pc, 4);
}

void kick(int, std::uint32_t address) {
    std::uint32_t q = address & 1023u, count = 0;
    bool done = false;
    while (!done && count < 1024) {
        Reg128 tag;
        std::memcpy(&tag, g_mem + q * 16, 16);
        const std::uint32_t nloop = static_cast<std::uint32_t>(tag.d[0]) & 0x7FFFu;
        const std::uint32_t flg = static_cast<std::uint32_t>(tag.d[0] >> 58) & 3u;
        std::uint32_t nreg = static_cast<std::uint32_t>(tag.d[0] >> 60) & 0xFu;
        if (!nreg) nreg = 16;
        const std::uint32_t body = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        done = (tag.d[0] >> 15) & 1u;
        for (std::uint32_t i = 0; i <= body && count < 1024; ++i, ++count, q = (q + 1) & 1023u) mix(g_mem + q * 16, 16);
    }
    g_kicked += count;
}

}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vubench <capture file> [passes]\n");
        return 1;
    }
    register_generated_vu();
    const int passes = argc > 2 ? std::atoi(argv[2]) : 20;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<Call> calls;
    for (;;) {
        Call c;
        std::uint32_t head[3];
        if (!in.read(reinterpret_cast<char *>(&c.key), 8)) break;
        in.read(reinterpret_cast<char *>(head), sizeof head);
        in.read(reinterpret_cast<char *>(&c.regs), sizeof c.regs);
        c.mem.resize(16384);
        in.read(reinterpret_cast<char *>(c.mem.data()), 16384);
        if (!in) break;
        c.pc = head[0];
        c.top = head[1];
        c.itop = head[2];
        calls.push_back(std::move(c));
    }
    std::size_t missing = 0;
    for (const Call &c : calls) missing += !g_programs.count(c.key);
    std::printf("%zu calls, %zu without a compiled program\n", calls.size(), missing);
    static std::uint8_t mem[16384];
    ps2::Vu0Regs regs;
    double best = 1e30;
    std::unordered_map<std::uint64_t, std::pair<double, std::uint64_t>> per_key;
    for (int pass = 0; pass < passes; ++pass) {
        g_hash = 0xCBF29CE484222325ull;
        g_escapes = g_kicked = 0;
        double spent = 0;
        for (const Call &c : calls) {
            const auto it = g_programs.find(c.key);
            if (it == g_programs.end()) continue;
            g_call = &c;
            g_mem = mem;
            regs = c.regs;
            std::memcpy(mem, c.mem.data(), sizeof mem);
            const auto start = std::chrono::steady_clock::now();
            it->second(regs, mem);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            spent += ms;
            if (pass == passes - 1) {
                per_key[c.key].first += ms;
                ++per_key[c.key].second;
            }
            mix(&regs, sizeof regs);
            mix(mem, sizeof mem);
        }
        best = std::min(best, spent);
    }
    std::vector<std::pair<std::uint64_t, std::pair<double, std::uint64_t>>> keys(per_key.begin(), per_key.end());
    std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.second.first > b.second.first; });
    for (const auto &[key, v] : keys) std::printf("  %016llX %8.3f ms %6llu calls\n", static_cast<unsigned long long>(key), v.first, static_cast<unsigned long long>(v.second));
    std::printf("best pass %.3f ms, checksum %016llX, %llu escapes, %llu qwords kicked\n", best, static_cast<unsigned long long>(g_hash),
                static_cast<unsigned long long>(g_escapes), static_cast<unsigned long long>(g_kicked));
    return 0;
}
