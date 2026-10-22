#include "window.hpp"
#include "pacing.hpp"

#include "thread_profile.hpp"

#include "ps2/system.hpp"

#include <SDL.h>
#include <SDL_syswm.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

enum Button : std::uint16_t {
    kSelect = 1u << 0, kL3 = 1u << 1, kR3 = 1u << 2, kStart = 1u << 3,
    kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7,
    kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10, kR1 = 1u << 11,
    kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15,
};

struct KeyBinding {
    SDL_Scancode key;
    std::uint16_t button;
};

std::vector<KeyBinding> g_keys = {
    {SDL_SCANCODE_UP, kUp},         {SDL_SCANCODE_DOWN, kDown},     {SDL_SCANCODE_LEFT, kLeft},
    {SDL_SCANCODE_RIGHT, kRight},   {SDL_SCANCODE_Z, kCross},       {SDL_SCANCODE_X, kCircle},
    {SDL_SCANCODE_A, kSquare},      {SDL_SCANCODE_S, kTriangle},    {SDL_SCANCODE_Q, kL1},
    {SDL_SCANCODE_W, kR1},          {SDL_SCANCODE_1, kL2},          {SDL_SCANCODE_2, kR2},
    {SDL_SCANCODE_RETURN, kStart},  {SDL_SCANCODE_BACKSPACE, kSelect},
};

enum Stick : std::uint16_t {
    kStickUp = 1, kStickDown = 2, kStickLeft = 4, kStickRight = 8,
    kRightUp = 16, kRightDown = 32, kRightLeft = 64, kRightRight = 128,
};
std::vector<KeyBinding> g_stick_keys = {
    {SDL_SCANCODE_UP, kStickUp},   {SDL_SCANCODE_DOWN, kStickDown}, {SDL_SCANCODE_LEFT, kStickLeft}, {SDL_SCANCODE_RIGHT, kStickRight},
    {SDL_SCANCODE_I, kStickUp},    {SDL_SCANCODE_K, kStickDown},    {SDL_SCANCODE_J, kStickLeft},    {SDL_SCANCODE_L, kStickRight},
    {SDL_SCANCODE_T, kRightUp},    {SDL_SCANCODE_G, kRightDown},    {SDL_SCANCODE_F, kRightLeft},    {SDL_SCANCODE_H, kRightRight},
};

SDL_Scancode scancode(std::string name) {
    if (name == "Enter") name = "Return";
    else if (name == "LShift") name = "Left Shift";
    else if (name == "RShift") name = "Right Shift";
    else if (name == "LCtrl") name = "Left Ctrl";
    else if (name == "RCtrl") name = "Right Ctrl";
    else if (name == "LAlt") name = "Left Alt";
    return SDL_GetScancodeFromName(name.c_str());
}

// PS2_KEYS names a controls file of lines like "circle=X,K" or "lup=Up,I"; listed inputs replace their defaults.
void load_keys() {
    const char *path = std::getenv("PS2_KEYS");
    if (!path) return;
    std::FILE *f = std::fopen(path, "r");
    if (!f) {
        std::fprintf(stderr, "[keys] cannot read %s\n", path);
        return;
    }
    static const std::pair<const char *, std::uint16_t> kButtons[] = {
        {"select", kSelect}, {"l3", kL3}, {"r3", kR3}, {"start", kStart}, {"up", kUp}, {"right", kRight}, {"down", kDown}, {"left", kLeft},
        {"l2", kL2}, {"r2", kR2}, {"l1", kL1}, {"r1", kR1}, {"triangle", kTriangle}, {"circle", kCircle}, {"cross", kCross}, {"square", kSquare}};
    static const std::pair<const char *, std::uint16_t> kSticks[] = {{"lup", kStickUp},    {"ldown", kStickDown},    {"lleft", kStickLeft},    {"lright", kStickRight},
                                                                     {"rup", kRightUp},    {"rdown", kRightDown},    {"rleft", kRightLeft},    {"rright", kRightRight}};
    char line[256];
    const auto trim = [](const std::string &text) {
        const auto first = text.find_first_not_of(" \t\r\n"), last = text.find_last_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} : text.substr(first, last - first + 1);
    };
    while (std::fgets(line, sizeof line, f)) {
        const std::string text = trim(line);
        const auto eq = text.find('=');
        if (text.empty() || text[0] == '#' || eq == std::string::npos) continue;
        const std::string name = trim(text.substr(0, eq));
        std::vector<KeyBinding> *list = nullptr;
        std::uint16_t value = 0;
        for (const auto &[n, v] : kButtons) if (name == n) list = &g_keys, value = v;
        for (const auto &[n, v] : kSticks) if (name == n) list = &g_stick_keys, value = v;
        if (!list) {
            std::fprintf(stderr, "[keys] unknown input %s\n", name.c_str());
            continue;
        }
        std::vector<KeyBinding> replacements;
        bool valid = true;
        for (std::size_t at = eq + 1; at <= text.size();) {
            const std::size_t comma = std::min(text.find(',', at), text.size());
            const std::string key = trim(text.substr(at, comma - at));
            at = comma + 1;
            if (key.empty()) continue;
            const SDL_Scancode code = scancode(key);
            if (code == SDL_SCANCODE_UNKNOWN) { std::fprintf(stderr, "[keys] unknown key %s; keeping previous mapping\n", key.c_str()); valid = false; }
            else replacements.push_back({code, value});
        }
        if (!valid) continue;
        list->erase(std::remove_if(list->begin(), list->end(), [&](const KeyBinding &b) { return b.button == value; }), list->end());
        list->insert(list->end(), replacements.begin(), replacements.end());
    }
    std::fclose(f);
}

struct PadBinding {
    SDL_GameControllerButton button;
    std::uint16_t mask;
};

constexpr PadBinding kPadButtons[] = {
    {SDL_CONTROLLER_BUTTON_A, kCross},          {SDL_CONTROLLER_BUTTON_B, kCircle},
    {SDL_CONTROLLER_BUTTON_X, kSquare},         {SDL_CONTROLLER_BUTTON_Y, kTriangle},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, kL1},  {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, kR1},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, kL3},     {SDL_CONTROLLER_BUTTON_RIGHTSTICK, kR3},
    {SDL_CONTROLLER_BUTTON_BACK, kSelect},      {SDL_CONTROLLER_BUTTON_START, kStart},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, kUp},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, kDown},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, kLeft},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, kRight},
};

SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_texture = nullptr;
SDL_AudioDeviceID g_audio = 0;
// the keyboard drives the first controller port along with the first game controller, a second one is player 2
SDL_GameController *g_pads[2] = {};
std::uint32_t g_tex_w = 0, g_tex_h = 0;
native_host::FrameDeadline g_deadline;
bool g_input_focused = true, g_threaded = false;
std::atomic<bool> g_audio_failed{false};
bool g_audio_started = false;
double g_audio_average = 96.0;
unsigned g_audio_fade = 240;
unsigned g_audio_underruns = 0;
std::atomic<int> g_save_status{0};
std::mutex g_host_input_mutex;
std::uint64_t g_host_pad[2] = {0xFFFF808080800000ull, 0xFFFF808080800000ull};
std::vector<std::uint32_t> g_pixels;
bool g_gpu_present = false;

bool create_renderer() {
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

bool attach_gpu() {
    if (std::getenv("PS2_SDL_RENDERER")) return false;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(g_window, &info)) return false;
    ps2::WindowTarget target;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    target.width = static_cast<std::uint32_t>(w);
    target.height = static_cast<std::uint32_t>(h);
    switch (info.subsystem) {
#if defined(SDL_VIDEO_DRIVER_X11)
    case SDL_SYSWM_X11:
        target.kind = ps2::WindowTarget::X11;
        target.display = info.info.x11.display;
        target.window = static_cast<std::uint64_t>(info.info.x11.window);
        break;
#endif
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    case SDL_SYSWM_WINDOWS:
        target.kind = ps2::WindowTarget::Windows;
        target.display = info.info.win.hinstance;
        target.surface = info.info.win.window;
        break;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
    case SDL_SYSWM_WAYLAND:
        target.kind = ps2::WindowTarget::Wayland;
        target.display = info.info.wl.display;
        target.surface = info.info.wl.surface;
        break;
#endif
    default: return false;
    }
    return ps2::attach_window(target);
}

std::uint8_t axis_value(int v) {
    constexpr int deadzone = 4096;
    if (std::abs(v) <= deadzone) return 0x80;
    v = v < 0 ? (v + deadzone) * 32768 / (32768 - deadzone) : (v - deadzone) * 32767 / (32767 - deadzone);
    return static_cast<std::uint8_t>(std::clamp((v + 32768) >> 8, 0, 255));
}

std::uint8_t axis(SDL_GameController *pad, SDL_GameControllerAxis a) { return axis_value(SDL_GameControllerGetAxis(pad, a)); }

void keyboard_input(const Uint8 *keys, std::uint16_t &pressed, std::uint8_t &lx, std::uint8_t &ly, std::uint8_t &rx, std::uint8_t &ry) {
    for (const KeyBinding &b : g_keys) if (keys[b.key]) pressed |= b.button;
    std::uint16_t stick = 0;
    for (const KeyBinding &b : g_stick_keys) if (keys[b.key]) stick |= b.button;
    const auto direction = [stick](unsigned low, unsigned high) { return static_cast<std::uint8_t>((stick & low) && !(stick & high) ? 0 : (stick & high) && !(stick & low) ? 255 : 128); };
    lx = direction(kStickLeft, kStickRight); ly = direction(kStickUp, kStickDown);
    rx = direction(kRightLeft, kRightRight); ry = direction(kRightUp, kRightDown);
}

void rumble(int port) {
    static double last_strong[2] = {};
    static bool last_weak[2] = {};
    static Uint32 sent[2] = {};
    SDL_GameController *pad = g_pads[port];
    if (!pad) return;
    const std::uint16_t motors = g_input_focused ? ps2::pad_rumble(port) : 0;
    const double strong = (motors >> 8) < 16 ? 0.0 : std::sqrt((motors >> 8) / 255.0);
    const bool weak = motors & 1u;
    const Uint32 now = SDL_GetTicks();
    if (std::fabs(strong - last_strong[port]) <= 0.08 && weak == last_weak[port] && (!(strong > 0.0 || weak) || now - sent[port] < 100)) return;
    last_strong[port] = strong;
    last_weak[port] = weak;
    sent[port] = now;
    SDL_GameControllerRumble(pad, static_cast<Uint16>(strong * 65535.0), weak ? 0xFFFF : 0, strong > 0.0 || weak ? 200 : 0);
}

// a new controller takes the first free port, the second one plugs in player 2
void open_pad(int device) {
    const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device);
    for (SDL_GameController *pad : g_pads)
        if (pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id) return;
    for (int port = 0; port < 2; ++port)
        if (!g_pads[port]) {
            g_pads[port] = SDL_GameControllerOpen(device);
            return;
        }
}

void publish_input(int port, std::uint64_t state) {
    const std::uint8_t axes[4] = {static_cast<std::uint8_t>(state >> 16), static_cast<std::uint8_t>(state >> 24), static_cast<std::uint8_t>(state >> 32), static_cast<std::uint8_t>(state >> 40)};
    std::uint8_t pressure[16];
    std::fill_n(pressure, 16, 0xFF);
    pressure[8] = static_cast<std::uint8_t>(state >> 48);
    pressure[9] = static_cast<std::uint8_t>(state >> 56);
    ps2::set_pad_input(port, static_cast<std::uint16_t>(state), axes, pressure);
}

void poll_port(int port) {
    static bool plugged[2] = {};
    SDL_GameController *pad = g_pads[port];
    if (port > 0 && plugged[port] != (pad != nullptr)) {
        plugged[port] = pad != nullptr;
        ps2::set_pad_connected(port, plugged[port]);
    }
    std::uint16_t pressed = 0;
    std::uint8_t l2 = 0xFF, r2 = 0xFF;
    std::uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    if (port == 0 && g_input_focused) keyboard_input(SDL_GetKeyboardState(nullptr), pressed, lx, ly, rx, ry);
    if (pad && g_input_focused) {
        for (const PadBinding &b : kPadButtons)
            if (SDL_GameControllerGetButton(pad, b.button)) pressed |= b.mask;
        const auto trigger = [&](SDL_GameControllerAxis a, std::uint16_t bit, std::uint8_t &pressure) {
            const int v = SDL_GameControllerGetAxis(pad, a);
            if (v <= 2000 || (pressed & bit)) return;
            pressed |= bit;
            pressure = static_cast<std::uint8_t>(std::max(1, v * 255 / 32767));
        };
        trigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT, kL2, l2);
        trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, kR2, r2);
        if (lx == 0x80) lx = axis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        if (ly == 0x80) ly = axis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (rx == 0x80) rx = axis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        if (ry == 0x80) ry = axis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
    }
    rumble(port);
    const std::uint64_t state = pressed | (static_cast<std::uint64_t>(rx) << 16) | (static_cast<std::uint64_t>(ry) << 24) | (static_cast<std::uint64_t>(lx) << 32) |
                                (static_cast<std::uint64_t>(ly) << 40) | (static_cast<std::uint64_t>(l2) << 48) | (static_cast<std::uint64_t>(r2) << 56);
    {
        std::lock_guard<std::mutex> lock(g_host_input_mutex);
        g_host_pad[port] = state;
        publish_input(port, state);
    }
}

void poll() {
    // PS2_QUICKSAVE_AT=frame presses the quick-save key once at that frame (for testing the F5 path).
    static std::uint64_t quick_at = std::getenv("PS2_QUICKSAVE_AT") ? std::strtoull(std::getenv("PS2_QUICKSAVE_AT"), nullptr, 10) : 0;
    if (quick_at && ps2::frames_run() >= quick_at) {
        quick_at = 0;
        const char *path = std::getenv("PS2_QUICKSAVE");
        g_save_status.store(1);
        ps2::request_save_state(path ? path : "quicksave.ps2");
    }
    static Uint32 last_poll = 0;
    if (g_threaded) {
        const Uint32 now = SDL_GetTicks();
        if (now - last_poll < 4) return;
        last_poll = now;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) ps2::halt("window closed");
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F5 && !e.key.repeat) {
            const char *path = std::getenv("PS2_QUICKSAVE");
            g_save_status.store(1);
            ps2::request_save_state(path ? path : "quicksave.ps2");
            SDL_SetWindowTitle(g_window, "ps2-web-recomp  saving state...");
        }
        if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED))
            g_input_focused = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
        if (e.type == SDL_AUDIODEVICEREMOVED && !e.adevice.iscapture && e.adevice.which == g_audio) g_audio_failed.store(true);
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F11 && !e.key.repeat) {
            const bool full = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN;
            SDL_SetWindowFullscreen(g_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            SDL_ShowCursor(full ? SDL_ENABLE : SDL_DISABLE);
        }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && g_gpu_present) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(g_window, &w, &h);
            ps2::resize_window(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED) open_pad(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED)
            for (SDL_GameController *&pad : g_pads)
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad);
                    pad = nullptr;
                }
    }
    poll_port(0);
    poll_port(1);
}

void show_pixels(const std::uint32_t *rgba, std::uint32_t w, std::uint32_t h) {
    if (w != g_tex_w || h != g_tex_h) {
        if (g_texture) SDL_DestroyTexture(g_texture);
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, static_cast<int>(w), static_cast<int>(h));
        SDL_RenderSetLogicalSize(g_renderer, static_cast<int>(w), static_cast<int>(w) * (ps2::g_options.widescreen ? 9 : 3) / (ps2::g_options.widescreen ? 16 : 4));
        g_tex_w = w;
        g_tex_h = h;
    }
    SDL_UpdateTexture(g_texture, nullptr, rgba, static_cast<int>(w * 4));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

void present() {
    if (g_gpu_present) {
        ps2::present_frame();
        return;
    }
    std::uint32_t w = 0, h = 0;
    if (ps2::display_frame(g_pixels, w, h)) show_pixels(g_pixels.data(), w, h);
}

constexpr Uint32 kAudioBytesPerMs = 48 * 4;

void pace_stats() {
    static const bool enabled = std::getenv("PS2_PACE_STATS") != nullptr;
    if (!enabled) return;
    static Uint64 last = 0;
    static std::vector<double> intervals;
    const Uint64 now = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    if (last) intervals.push_back((now - last) * 1000.0 / frequency);
    last = now;
    double elapsed = 0.0;
    for (double ms : intervals) elapsed += ms;
    if (elapsed < 1000.0) return;
    const double rate = intervals.size() * 1000.0 / elapsed;
    std::sort(intervals.begin(), intervals.end());
    std::fprintf(stderr, "[pace] %.2f fields/s p95 %.2f ms max %.2f ms audio %.1f ms underruns %u clock %s\n", rate,
                 intervals[intervals.size() * 95 / 100], intervals.back(), g_audio ? SDL_GetQueuedAudioSize(g_audio) / static_cast<double>(kAudioBytesPerMs) : 0.0,
                 g_audio_underruns, g_audio_started && !g_audio_failed.load() ? "audio" : "video");
    intervals.clear();
}

void pace() {
    pace_stats();
    double adjustment = 0.0;
    if (g_audio && g_audio_started && !g_audio_failed.load()) {
        // Smooth the device's block-sized queue updates rather than releasing fields in bursts.
        native_host::AudioProgress progress{SDL_GetQueuedAudioSize(g_audio), SDL_GetTicks()};
        if (SDL_GetAudioDeviceStatus(g_audio) != SDL_AUDIO_PLAYING) g_audio_failed.store(true);
        while (!g_audio_failed.load() && !ps2::halted() && SDL_GetQueuedAudioSize(g_audio) > 120 * kAudioBytesPerMs) {
            const Uint32 queued = SDL_GetQueuedAudioSize(g_audio), now = SDL_GetTicks();
            if (g_audio_failed.load() || SDL_GetAudioDeviceStatus(g_audio) != SDL_AUDIO_PLAYING || progress.stalled(queued, now)) {
                g_audio_failed.store(true);
                std::fprintf(stderr, "[audio] device stopped draining; using video pacing\n");
                break;
            }
            if (!g_threaded) poll();
            SDL_Delay(1);
        }
        g_audio_average += (SDL_GetQueuedAudioSize(g_audio) / static_cast<double>(kAudioBytesPerMs) - g_audio_average) * 0.02;
        if (!g_audio_failed.load()) adjustment = std::clamp((g_audio_average - 96.0) * 0.00005, -0.001, 0.001);
    } else g_audio_average = 96.0;
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    const double deadline = g_deadline.advance(SDL_GetPerformanceCounter() / frequency, ps2::video_timing().field_seconds(), adjustment);
    while (!ps2::halted() && SDL_GetPerformanceCounter() / frequency < deadline) {
        if (!g_threaded) poll();
        const double left = deadline - SDL_GetPerformanceCounter() / frequency;
        if (left > 0.0) SDL_Delay(static_cast<Uint32>(std::max(1.0, left * 1000.0)));
    }
}

double process_cpu_seconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    const auto seconds = [](const FILETIME &t) { return static_cast<double>((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 1e7; };
    return seconds(kernel) + seconds(user);
#else
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
#endif
}

double resident_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) return 0.0;
    return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#else
    long pages = 0, resident = 0;
    if (std::FILE *f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

void show_speed() {
    static std::uint64_t start = 0, frames = 0;
    static double cpu_start = 0.0, gpu_start = 0.0;
    const std::uint64_t now = SDL_GetPerformanceCounter();
    if (!start) {
        start = now;
        cpu_start = process_cpu_seconds();
        gpu_start = ps2::gpu_busy_ms();
    }
    ++frames;
    const double seconds = static_cast<double>(now - start) / static_cast<double>(SDL_GetPerformanceFrequency());
    if (seconds < 1.0) return;
    const double cpu = process_cpu_seconds(), gpu = ps2::gpu_busy_ms();
    char title[192];
    const double queued = g_audio ? SDL_GetQueuedAudioSize(g_audio) / static_cast<double>(kAudioBytesPerMs) : 0.0;
    std::snprintf(title, sizeof title, "ps2-web-recomp  %.1f fps  CPU %.0f%%  GPU %.0f%%  RAM %.0f MB  audio %.0f ms", static_cast<double>(frames) / seconds,
                  (cpu - cpu_start) / seconds * 100.0, (gpu - gpu_start) / (seconds * 10.0), resident_mb(), queued);
    SDL_SetWindowTitle(g_window, title);
    const int saved = g_save_status.load();
    if (saved) {
        SDL_SetWindowTitle(g_window, saved == 1 ? "Devil May Cry - saving quick save..." : saved == 2 ? "Devil May Cry - quick save saved (resume from launcher)" : "Devil May Cry - quick save failed; see game log");
        if (saved != 1) g_save_status.store(0);
    }
    start = now;
    frames = 0;
    cpu_start = cpu;
    gpu_start = gpu;
}

void on_vblank() {
    show_speed();
    poll();
    present();
    pace();
}

void queue_audio(const std::int16_t *samples, std::size_t frames) {
    if (!g_audio || g_audio_failed.load()) return;
    const Uint32 queued = SDL_GetQueuedAudioSize(g_audio);
    if ((g_audio_started && !queued) || queued > 250 * kAudioBytesPerMs) {
        if (g_audio_started && !queued) ++g_audio_underruns;
        SDL_PauseAudioDevice(g_audio, 1);
        SDL_ClearQueuedAudio(g_audio);
        g_audio_started = false;
        g_audio_fade = 240;
    }
    std::vector<std::int16_t> faded;
    if (g_audio_fade) {
        faded.assign(samples, samples + frames * 2);
        for (std::size_t n = 0; n < frames && g_audio_fade; ++n, --g_audio_fade)
            for (unsigned ch = 0; ch < 2; ++ch) faded[n * 2 + ch] = static_cast<std::int16_t>(faded[n * 2 + ch] * (240 - g_audio_fade) / 240);
        samples = faded.data();
    }
    if (SDL_QueueAudio(g_audio, samples, static_cast<Uint32>(frames * 4)) != 0) {
        g_audio_failed.store(true);
        std::fprintf(stderr, "[audio] queue failed: %s\n", SDL_GetError());
    } else if (!g_audio_started && SDL_GetQueuedAudioSize(g_audio) >= 60 * kAudioBytesPerMs) {
        SDL_PauseAudioDevice(g_audio, 0);
        g_audio_started = true;
    }
}

}

bool open_window(bool fullscreen) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    load_keys();
    const Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u);
    g_window = SDL_CreateWindow("ps2-web-recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ps2::g_options.widescreen ? 1280 : 960, 720, flags);
    if (!g_window) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    g_audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio) {
        ps2::set_audio_sink(queue_audio);
    }
    g_input_focused = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    ps2::g_options.on_state_saved = [](bool saved) { g_save_status.store(saved ? 2 : -1); };
    ps2::g_options.on_state_loaded = [] {
        std::lock_guard<std::mutex> lock(g_host_input_mutex);
        for (int port = 0; port < 2; ++port) publish_input(port, g_host_pad[port]);
    };
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) open_pad(i);
    if (fullscreen) SDL_ShowCursor(SDL_DISABLE);
    return true;
}

int run_window(const std::function<int()> &emulate) {
    ps2::init_graphics();
    g_gpu_present = attach_gpu();
    std::fprintf(stderr, "[window] %s\n", g_gpu_present ? "presenting through WebGPU" : "presenting through SDL");
    if (!g_gpu_present && !create_renderer()) return 1;
    const bool threads = !std::getenv("PS2_ONE_THREAD") && !std::getenv("PS2_TIME") && ps2::g_options.frames_dir.empty();
    g_threaded = threads;
    if (!threads) {
        ps2::g_options.on_vblank = on_vblank;
        return emulate();
    }
    ps2::g_options.on_vblank = [] {
        ps2::post_present();
        pace();
    };
    ps2::start_render_thread();
    if (!std::getenv("PS2_NO_VU_THREAD")) {
        ps2::start_vu_thread();
        std::thread([] {
            profile_this_thread("vu");
            ps2::run_vu_thread();
        }).detach();
    }
    std::atomic<bool> done{false};
    int result = 0;
    std::thread emulation([&] {
        profile_this_thread("emu");
        result = emulate();
        done.store(true);
        ps2::wake_render_thread();
    });
    const std::function<void(const std::uint32_t *, std::uint32_t, std::uint32_t)> copy = show_pixels, none;
    profile_this_thread("render");
    while (!done.load()) {
        poll();
        if (ps2::render_pending(g_gpu_present, g_gpu_present ? none : copy, show_speed, 8.0) == ps2::Rendered::idle) ps2::wait_render_work(4);
    }
    emulation.join();
    write_thread_profile();
    SDL_Quit();
    std::fflush(nullptr);
    std::_Exit(result);
}
