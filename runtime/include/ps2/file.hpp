#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ps2 {

// Stage beside the destination so replacing an existing save never truncates it.
inline bool write_file_atomic(const std::string &name, const void *data, std::size_t size) {
    static std::atomic<unsigned> sequence{0};
    const std::filesystem::path target(name);
    const std::filesystem::path temporary(name + ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(sequence++));
    bool written = false;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
            out.flush();
            out.close();
            written = static_cast<bool>(out);
        }
    }
    if (written) {
#ifdef _WIN32
        written = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        written = std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
    }
    if (!written) { std::error_code error; std::filesystem::remove(temporary, error); }
    return written;
}

}
