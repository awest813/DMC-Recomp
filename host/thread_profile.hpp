#pragma once

// Sampling profiler for the native window threads (Windows only; no-op elsewhere).
void profile_this_thread(const char *name);
void write_thread_profile();
