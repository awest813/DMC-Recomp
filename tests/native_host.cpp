// Include the host to exercise its internal SDL policies without a game or test-only runtime API.
#define SDL_MAIN_HANDLED
#include "../host/window.cpp"
#include "ps2/file.hpp"
#include "ps2/state.hpp"
#include "iop/sio2.hpp"

#include <array>
#include <filesystem>
#include <fstream>

void serial_transfer(unsigned port, std::initializer_list<std::uint8_t> command) {
    using namespace ps2::iop::sio2;
    write(0x1F808268, 0xC);
    write(0x1F808200, port | (static_cast<unsigned>(command.size()) << 8) | (16 << 18));
    for (auto byte : command) write(0x1F808260, byte);
}
void card_transfer(std::initializer_list<std::uint8_t> command) { serial_transfer(2, command); }

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--read-card") {
        ps2::g_options.memcard_path = argv[2];
        ps2::iop::sio2::reset();
        card_transfer({0x81, 0x43, 3});
        std::vector<unsigned> bytes;
        for (unsigned n = 0; n < 7; ++n) bytes.push_back(ps2::iop::sio2::read(0x1F808264));
        return bytes[4] == 0x12 && bytes[5] == 0x34 && bytes[6] == 0x56 ? 0 : 1;
    }
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char *name) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", name); } };
    native_host::FrameDeadline clock;
    double now = 100.0;
    for (unsigned frame = 0; frame < 10000; ++frame) now = clock.advance(now, 100.0 / 5994);
    check(std::abs(now - (100.0 + 10000 * 100.0 / 5994)) < 0.000001, "fractional deadlines do not drift");
    check(std::abs(clock.advance(now + 1.0, 0.02) - (now + 1.02)) < 0.000001, "stall and mode change rebase deadline");
    native_host::AudioProgress progress{20000, 0};
    check(!progress.stalled(20000, 249) && progress.stalled(20000, 250), "stalled queue wait is bounded");
    check(!progress.stalled(19000, 260) && !progress.stalled(19000, 509) && progress.stalled(19000, 510), "queue progress resets watchdog");
    for (int value : {-4096, -1, 0, 1, 4096}) check(axis_value(value) == 128, "stick deadzone");
    check(axis_value(-32768) == 0 && axis_value(32767) == 255, "full stick range");
    std::array<Uint8, SDL_NUM_SCANCODES> keys{};
    std::uint16_t buttons = 0;
    std::uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    keys[SDL_SCANCODE_LEFT] = keys[SDL_SCANCODE_RIGHT] = 1;
    keyboard_input(keys.data(), buttons, lx, ly, rx, ry);
    check(lx == 128 && (buttons & (kLeft | kRight)) == (kLeft | kRight), "opposing stick keys cancel; D-pad stays digital");
    keys[SDL_SCANCODE_LEFT] = 0; keys[SDL_SCANCODE_S] = 1;
    keyboard_input(keys.data(), buttons, lx, ly, rx, ry);
    check(lx == 255 && (buttons & kTriangle), "keyboard stick and action mapping");

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    check(open_window(false), "open SDL dummy window/audio");
    ps2::boot(0x100000, {});
    const std::vector<std::int16_t> sound(3200 * 2, 12000);
    queue_audio(sound.data(), 1000);
    check(!g_audio_started && SDL_GetAudioDeviceStatus(g_audio) == SDL_AUDIO_PAUSED, "startup prebuffers");
    queue_audio(sound.data(), 2200);
    check(g_audio_started, "enough PCM starts playback");
    SDL_PauseAudioDevice(g_audio, 1);
    SDL_ClearQueuedAudio(g_audio);
    SDL_QueueAudio(g_audio, sound.data(), 3200 * 4);
    queue_audio(sound.data(), 1000);
    const Uint32 started = SDL_GetTicks();
    pace();
    check(g_audio_failed.load() && SDL_GetTicks() - started < 500, "paused audio cannot trap pacing");
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT; event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&event); poll();
    check(!g_input_focused, "focus loss blocks game input");
    event.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    SDL_PushEvent(&event); poll();
    check(g_input_focused, "focus return restores input");

    const auto directory = std::filesystem::temp_directory_path() / ("ps2-native-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "card.ps2";
    ps2::set_pad_buttons(kTriangle);
    const auto snapshot = directory / "held.ps2";
    check(ps2::save_state(snapshot.string()), "snapshot with a held button");
    ps2::set_pad_buttons(0);
    check(ps2::load_state(snapshot.string()), "load held-button snapshot");
    serial_transfer(0, {1, 0x42, 0, 0, 0});
    for (unsigned n = 0; n < 3; ++n) ps2::iop::sio2::read(0x1F808264);
    check(ps2::iop::sio2::read(0x1F808264) == 0xFF && ps2::iop::sio2::read(0x1F808264) == 0xFF, "native load republishes neutral input despite unchanged cache");
    ps2::set_pad_buttons(kTriangle);
    poll_port(0);
    serial_transfer(0, {1, 0x42, 0, 0, 0});
    for (unsigned n = 0; n < 3; ++n) ps2::iop::sio2::read(0x1F808264);
    check(ps2::iop::sio2::read(0x1F808264) == 0xFF && ps2::iop::sio2::read(0x1F808264) == 0xFF, "unchanged host input is refreshed after guest input changes");
    serial_transfer(0, {1, 0x44, 0, 1, 3});
    std::atomic<bool> writing{true}, ready{false};
    std::thread writer([&] {
        std::uint8_t pressure[16]; std::fill_n(pressure, 16, 255);
        unsigned n = 0;
        while (writing.load()) {
            const std::uint8_t value = ++n & 1 ? 0x11 : 0xEE;
            const std::uint8_t axes[4] = {value, value, value, value};
            ps2::set_pad_input(0, value * 257, axes, pressure);
            ready.store(true);
        }
    });
    while (!ready.load()) std::this_thread::yield();
    for (unsigned n = 0; n < 1000; ++n) {
        serial_transfer(0, {1, 0x42, 0, 0, 0, 0, 0, 0, 0});
        unsigned response[9];
        for (unsigned i = 0; i < 9; ++i) response[i] = ps2::iop::sio2::read(0x1F808264);
        if (response[3] == 0xEE || response[3] == 0x11)
            check(response[3] == response[4] && response[5] == (response[3] ^ 255) && response[5] == response[6] && response[6] == response[7] && response[7] == response[8], "controller packet contains one coherent host update");
    }
    writing.store(false); writer.join();
    check(ps2::write_file_atomic(path.string(), "first", 5), "create save atomically");
    check(ps2::write_file_atomic(path.string(), "second", 6), "replace save atomically");
    std::ifstream in(path, std::ios::binary);
    check(std::string(std::istreambuf_iterator<char>(in), {}) == "second", "replacement contains complete new data");
    in.close();
    check(!ps2::write_file_atomic(directory.string(), "bad", 3), "failed replacement preserves destination directory");
    ps2::g_options.memcard_path = (directory / "missing" / "memory.ps2").string();
    ps2::iop::sio2::reset();
    card_transfer({0x81, 0x42, 3, 0x12, 0x34, 0x56});
    ps2::iop::sio2::flush_cards(true);
    check(!std::filesystem::exists(ps2::g_options.memcard_path), "failed card write does not create a partial save");
    std::filesystem::create_directory(directory / "missing");
    ps2::iop::sio2::flush_cards(true);
    std::ifstream card(ps2::g_options.memcard_path, std::ios::binary);
    unsigned char bytes[3]{}; card.read(reinterpret_cast<char *>(bytes), 3); card.close();
    check(bytes[0] == 0x12 && bytes[1] == 0x34 && bytes[2] == 0x56, "failed card write retains dirty data for retry");
    check(std::filesystem::file_size(ps2::g_options.memcard_path) == 8650752, "complete PS2 card size");
    card_transfer({0x81, 0x23, 0, 0, 0, 0});
    card_transfer({0x81, 0x43, 3});
    for (unsigned n = 0; n < 4; ++n) ps2::iop::sio2::read(0x1F808264);
    check(ps2::iop::sio2::read(0x1F808264) == 0x12 && ps2::iop::sio2::read(0x1F808264) == 0x34 && ps2::iop::sio2::read(0x1F808264) == 0x56, "memory-card protocol reads written bytes");
    const auto mapping = directory / "controls.txt";
    { std::ofstream file(mapping); file << " \ttriangle = Space \t\nlup= U \ncircle=NotARealKey\n"; }
#ifdef _WIN32
    _putenv_s("PS2_KEYS", mapping.string().c_str());
#else
    setenv("PS2_KEYS", mapping.string().c_str(), 1);
#endif
    load_keys();
    keys.fill(0); keys[SDL_SCANCODE_SPACE] = keys[SDL_SCANCODE_U] = 1; buttons = 0;
    keyboard_input(keys.data(), buttons, lx, ly, rx, ry);
    check((buttons & kTriangle) && ly == 0, "custom action and stick bindings");
    keys.fill(0); keys[SDL_SCANCODE_S] = keys[SDL_SCANCODE_I] = 1; buttons = 0;
    keyboard_input(keys.data(), buttons, lx, ly, rx, ry);
    check(!(buttons & kTriangle) && ly == 128, "custom bindings replace old defaults");
    keys.fill(0); keys[SDL_SCANCODE_X] = 1; buttons = 0;
    keyboard_input(keys.data(), buttons, lx, ly, rx, ry);
    check((buttons & kCircle) != 0, "invalid remapping preserves the previous binding");
    for (const auto &entry : std::filesystem::directory_iterator(directory)) check(entry.path().filename().string().find(".tmp.") == std::string::npos, "no staging files left behind");
    if (std::filesystem::canonical(directory).parent_path() == std::filesystem::canonical(std::filesystem::temp_directory_path())) std::filesystem::remove_all(directory);
    else check(false, "temporary test directory stays inside its parent");
    SDL_Quit();
    std::printf("%d/%d native host checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
