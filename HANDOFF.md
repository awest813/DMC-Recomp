# Devil May Cry Port - Handoff

Last updated: 2026-10-22. Branch `dmc-port`.

Target: Devil May Cry, NTSC-U (`SLUS_202.16`), disc image at `..\Devil May Cry.iso` (outside the repository).

## Status

Devil May Cry plays from a fresh boot through the title, the intro movie, the real-time opening cutscenes and the prologue into Mission 1 (the castle's grand hall and upper gallery), natively and in the browser.

| Area | State |
|---|---|
| Native (Windows) | SDL2 + wgpu-native, threaded EE/VU1/GS, nominal 59.94 fields/s with audio-corrected deadlines. Ordinary p95 field intervals ~18–19 ms in a one-minute check. See `docs/NATIVE_STABILITY.md`. |
| Web (Chrome/Edge) | Builds with Emscripten 6.0.11; Mission 1 holds 60 fps in headless Chrome. Without WebGPU the software renderer takes over at ~10 fps. |
| Rendering | GPU output is deterministic and pinned by golden frames; the software renderer matches it (mean <= 0.15, p99 <= 1). 1x/2x/3x internal resolution. |
| Controls | Pad mapping verified against the game's own Controller Setup screen; keyboard remappable (`controls.txt`); arrows drive the left stick. |
| Audio | No clipping or dropouts at the title, movie, cutscene and gameplay; locked to the game clock. |
| Tooling | Launcher, regression suite, explorer, headless browser probe, audio check, many trace hooks. |

## Play

Double-click **`Play Devil May Cry.bat`** in the repository root. The launcher has Play, Settings, and Controls & help pages; see [launcher/README.md](launcher/README.md).

- **Play**: select Window or Browser, then Start game. Resume quick save loads the last F5 snapshot in either version. In a window, F5 writes `games/dmc/quicksave.ps2` and F11 toggles fullscreen.
- **Settings**: disc, window memory card, picture quality and display options. Browser cards are separate; import/export them inside the game. The original localhost browser origin is preserved.
- **Controls & help**: default controls, a scrolling keyboard editor, and the game log. Settings and mappings carry over from the old launcher.
- Browser play verifies the local server's disc/build before reuse and stops only a server owned by this launcher. Closing an owned active server asks first.
- The browser player now has a ready screen, sound toggle, volume, settings and a current keyboard guide. Alt+Enter toggles fullscreen; F remains a right-stick key. Picture changes require Apply & restart. Panels suppress game inputs while open but do not pause the game. See [web/README.md](web/README.md).

Default keys: arrows or I/J/K/L move, S action/jump (Triangle), X sword (Circle), A shoot (Square), Z examine/confirm (Cross), W aim/lock on (R1), Q devil trigger (L1), 1 map (L2), T/F/G/H right stick (devil arm), Enter menu (Start), Backspace options (Select). DMC moves Dante only with the left stick; the D-pad is for menus.

The native build (with `SDL2.dll` next to `ps2run.exe`) and the web build must exist first; see Build.

GitHub Pages hosts a local ISO/build picker at https://awest813.github.io/DMC-Recomp/. Choose the supported disc and four locally built `ps2web` files; no game artifacts are hosted. Scoped service-worker isolation and bounded local range reads work without a local server. See [web/README.md](web/README.md#github-pages-iso-picker) for deployment and tests.

## Build

From the repository root, in PowerShell. One-time downloads are already in place here: `third_party/wgpu-native` (v29.0.1.1, windows-x86_64-gnu), `third_party/SDL2-2.32.10` (MinGW devel) and `tools/emsdk` (6.0.11). None of them are committed.

```powershell
# recompilers
cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
ninja -C build/tools eerecomp vurecomp

# translate the game (only when the recompiler or entry list changes); then compile the recorded VU1 programs
& build/tools/eerecomp.exe 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' --entries generated/dmc/entries.txt --out generated/dmc
& build/tools/vurecomp.exe games/dmc/vu1 generated/dmc

# native runner with a window
cmake -S . -B build/native-dmc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ `
  "-DPS2_GENERATED=$((Get-Location).Path)/generated/dmc" `
  "-DSDL2_DIR=$((Get-Location).Path)/third_party/SDL2-2.32.10/x86_64-w64-mingw32/lib/cmake/SDL2"
ninja -C build/native-dmc ps2run
Copy-Item third_party/SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll build/native-dmc/

# web build, then serve it
powershell -ExecutionPolicy Bypass -File scripts/build_web.ps1 dmc
python scripts/serve.py build/web-dmc "..\Devil May Cry.iso" 8620 --record games/dmc/vu1-web
```

- The boot ELF and `SYSTEM.CNF` the web build packs are in `games/dmc/disc/` (copied from the disc).
- Re-run the CMake configure after adding generated files (new `vu1_*.cpp`); the sources are globbed.
- The first web build takes several minutes (65 MB of generated C++); runtime-only rebuilds are quick.
- If Windows Defender quarantines `tools/emsdk/upstream/bin/wasm-opt.exe` again, web links fail with "binaryen executable not found". Restore it from Windows Security > Protection history.

## Test

| Command | What it does |
|---|---|
| `python scripts/dmc_regress.py` | 9 cases: boot to title, Options menu, shop cutscene, Mission 1 idle/jump/walk, castle hall, sealed door, upper gallery. Fails on a crash, early halt, unhandled syscall, missing IOP import, missing code, a jump that does not land, a walk that does not move, or a screenshot that differs from `games/dmc/golden` (mean > 0.25 or p99 > 12). `--record` refreshes the golden frames; `PS2_GS=soft` checks the software renderer against them. |
| `python scripts/dmc_explore.py <state> <steps> --start-frame N [--stop-at-battle]` | Greedy explorer: from each checkpoint tries the four stick directions with jumps or Cross taps, keeps the run that ends farthest from where Dante has been (height weighted), reports crashes and missing code, and stops when the battle flag sets. |
| `powershell -File scripts/webprobe.ps1`, then `node scripts/webprobe.mjs <url> <seconds> [--shot f.png] [--every ms] [--profile s] [--nogpu]` | Headless Chrome harness for the web build: fps/status line, console, screenshot, per-worker CPU profile, and a no-WebGPU mode. `webprobe.ps1 -Stop` closes it. Use it instead of the Claude browser pane for timing; the pane is throttled to ~1 fps when the app window is in the background. |
| `PS2_DUMP_AUDIO=out.wav ...`, then `python scripts/audio_check.py out.wav [--png dir]` | Audio sanity: clipping, DC, silence, dropouts, stereo, loudness strip, spectrogram. |

Save states in `games/dmc` (game data; local only). Pass `--frames` as an absolute frame number when loading one.

| State | Where |
|---|---|
| `options1300.ps2` | Title screen, Options menu open |
| `cut9000.ps2` | Real-time opening cutscene in Dante's shop |
| `s16000.ps2` | Mallet Island cutscene, just before the prologue |
| `ground17000.ps2` | Prologue: Dante on the bridge, in control (regression base) |
| `hall21900.ps2` | Mission 1 just started, castle grand hall |
| `hall22800.ps2` | Grand hall, by the sealed red door |
| `upstairs28380.ps2` | Grand hall upper gallery |
| `quicksave.ps2` | Whatever F5 saved last |

## Open issues

Frame/rendering audit: see [docs/ACCURACY_AUDIT.md](docs/ACCURACY_AUDIT.md). Shared nominal profiles now cover NTSC/PAL interlaced, double-strike and 480p timing, including rational deadlines and delayed GS FIELD updates; see [docs/VIDEO_TIMING.md](docs/VIDEO_TIMING.md). Both renderers handle 24-bit DATE correctly and implement 16-bit dithering. Disc-free checks are available with `-DPS2_BUILD_ACCURACY_TESTS=ON`. Custom video modes and fixed-point interpolation remain open. Golden frames were refreshed for the intended video timing change; the old set remains local.

1. **Combat is untested.** Enemies are story-triggered and the explorer has not met any. The plan is to use an F5 quick-save taken at the first fight.
2. **The game's first formatted-card save/load remains untested.** Raw SIO2 card read/write, fresh-process reads, failed-write retries and orderly-exit flushing are tested. New cards start unformatted; DMC should offer to format at its first save point, which has not been reached.
3. **SPU2 accuracy**: Gaussian interpolation, per-voice buses, MMIX dry routing and the core output chain are implemented; see [docs/AUDIO_ACCURACY.md](docs/AUDIO_ACCURACY.md). Reverb/wet MMIX, voice noise, pitch modulation and detailed envelope timing remain incomplete. New states use version 5 for video events and load versions 2-4. Gaussian history is present from version 4 onward; earlier states initialize it to zero.
4. **Unconfirmed cutscene effect**: hard-edged rectangular fire patches on Dante in the stabbing cutscene (all resolutions). Possibly correct; needs a hardware or PCSX2 reference.
5. **Software renderer speed in the browser** (~10 fps; the GS thread is the bottleneck). Only matters without WebGPU.
6. Without WebGPU the page can log a one-off `requestAdapter` TypeError from WebGPU glue code; the game still runs.

## Next steps

1. Load the F5 quick-save from the first fight; check enemy rendering, effects, the HUD gauge (shown during battles) and hit reactions. Add it as a regression case.
2. Play to the first save point to exercise card formatting and saving, then **Load Data** from the title.
3. Continue SPU2 accuracy with reverb/wet MMIX, noise/pitch modulation and hardware-derived envelope tests. Dry routing and the core output chain are complete.
4. Compare the questionable cutscene frames with PCSX2.

## Where things are

- `runtime/` - the PS2 runtime: EE kernel HLE (`src/kernel.cpp`), VU (`include/ps2/vu*.hpp`, `src/vu_*.cpp`), GS (`src/gs/`, GPU in `src/gs/gpu/`), IOP and its HLE libraries (`src/iop/`), IPU, DMA, VIF.
- `recomp/` - `eerecomp` (EE to C++) and `vurecomp` (VU1 microprograms to C++).
- `host/main.cpp` (headless runner and crash handler), `host/window.cpp` (SDL window, keys, quick-save), `host/web.cpp` (browser host, disc streaming).
- `web/shell.html` - the browser page (input, keys, GPU choice, status line).
- `launcher/launcher.ps1` + `Play Devil May Cry.bat` - the Windows launcher.
- `scripts/` - build, serve, test and analysis scripts (see Test).
- `generated/dmc/` - generated game code; never hand-edit. `games/dmc/` - local game data, logs, states, golden frames.
- `AGENTS.md` - rules and the full debugging reference. `ENGINEERING_LOG.md` - how each problem was found and fixed. `PROGRESS_PLAN.md` - phases and remaining checks.

### Latest preview / surface speckles

The GPU now carries the earlier software renderer's stable triangle ordering and preserves constant interpolated colors exactly, fixing the regular stone-surface dots reproduced in the torch corridor at 3x. Explicit mip levels and minification filtering are implemented on both renderers; see `docs/ACCURACY_AUDIT.md`. Goldens were intentionally refreshed and the old set is retained as `games/dmc/golden-before-mipmapping`. Local preview: `http://127.0.0.1:8620/ps2web.html?state=sparkle-corridor.ps2`. Source checkpoint is `games/dmc/explore/step04_ldown.ps2`; no game data is tracked. Automatic MTBA chains and hardware-exact LOD/filter precision remain open.

### Latest audio work

Per-voice dry/wet masks gate all 24 voices independently on both cores; dry MMIX and core-to-core output routing are implemented. SPU2 tests: 37,479 assertions. Four core-routing captures show no clipping/dropouts and differ by at most three sample levels from the preceding routing captures. Reverb/wet MMIX and other detailed SPU2 work remain open; see `docs/AUDIO_ACCURACY.md`.

For the reported scratchy browser startup, the worklet now prebuffers 60 ms at startup/recovery and uses 5 ms fades on starvation/resume. `node scripts/test_web_audio.mjs` tests the actual worklet, including steady PCM, recovery and counter wrap. See `docs/AUDIO_ACCURACY.md` and the latest engineering log entries for validation and limits.

### Native session stability

The native host now prebuffers sound, fades recovery, bounds audio-device stalls and uses continuous mode-derived deadlines with gentle queue correction. Focus loss neutralizes input, sticks have a rescaled deadzone, and the SDL event loop is limited to 250 polls/s. Input publication/serialization is synchronized, and native checkpoint loads immediately reapply live input. F5 reports save completion; resume uses the launcher. Checkpoint/card files use staged replacement, card errors keep pending data for retry, and shutdown flushes dirty cards. Save formats are unchanged. Quick-saving still briefly pauses emulation and can cause one audio recovery; see [docs/NATIVE_STABILITY.md](docs/NATIVE_STABILITY.md).
