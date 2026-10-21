#include "thread_profile.hpp"

// Windows sampling profiler for the native window's threads. Set
// PS2_THREAD_PROFILE=<file>; each thread that calls profile_this_thread() is
// suspended about once a millisecond and its instruction pointer counted. The
// output lists "thread module rva count"; scripts/thread_profile.py turns the
// rvas of ps2run.exe into symbols with llvm-nm.

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Sampled {
    std::string name;
    HANDLE handle;
    std::map<std::uintptr_t, std::uint32_t> hits;
    std::uint64_t samples = 0;
};

std::mutex g_mutex;
std::vector<Sampled *> g_threads;
std::atomic<bool> g_running{false};
std::atomic<bool> g_written{false};

const char *path() {
    static const char *p = std::getenv("PS2_THREAD_PROFILE");
    return p;
}

void sampler() {
    timeBeginPeriod(1);
    while (g_running.load()) {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (Sampled *t : g_threads) {
                if (SuspendThread(t->handle) == static_cast<DWORD>(-1)) continue;
                CONTEXT c{};
                c.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(t->handle, &c)) {
                    ++t->hits[static_cast<std::uintptr_t>(c.Rip)];
                    ++t->samples;
                }
                ResumeThread(t->handle);
            }
        }
        Sleep(1);
    }
}

}

void profile_this_thread(const char *name) {
    if (!path()) return;
    HANDLE h = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_threads.push_back(new Sampled{name, h, {}, 0});
    if (!g_running.exchange(true)) std::thread(sampler).detach();
}

void write_thread_profile() {
    if (!path() || g_written.exchange(true)) return;
    g_running.store(false);
    std::lock_guard<std::mutex> lock(g_mutex);
    FILE *f = std::fopen(path(), "w");
    if (!f) return;
    for (Sampled *t : g_threads) {
        std::fprintf(f, "# thread %s samples %llu\n", t->name.c_str(), static_cast<unsigned long long>(t->samples));
        for (const auto &[pc, n] : t->hits) {
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            std::uintptr_t base = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(pc), &module)) {
                GetModuleFileNameA(module, name, sizeof name);
                base = reinterpret_cast<std::uintptr_t>(module);
            }
            const char *slash = std::strrchr(name, '\\');
            std::fprintf(f, "%s %s %llx %u\n", t->name.c_str(), slash ? slash + 1 : name, static_cast<unsigned long long>(pc - base), n);
        }
    }
    std::fclose(f);
}

#else

void profile_this_thread(const char *) {}
void write_thread_profile() {}

#endif
