# Devil May Cry Web Port - Handoff

Last updated: 2026-10-19 (session 2)

Workspace: `C:\Users\allen\Downloads\ps2-web-recomp-main\ps2-web-recomp-main`

Target: `SLUS_202.16` / `Devil May Cry.iso`

## Bottom line

The black screen after the title is fixed. Native runs boot -> title -> intro FMV -> real-time opening cutscene -> Mission 1 gameplay (Dante controllable, HUD, item menu) from a fresh boot. The web build compiles and runs in Chrome/Edge (WebGPU) at 60 fps through the intro FMV.

- Native build: works, with the wgpu GPU renderer (Intel Arc A750 here).
- Boot to title: works.
- Post-title transition: fixed (zombie-thread bug in the EE kernel HLE).
- Gameplay: Mission 1 reached around frame 17000 with no missing code.
- Speed: ~6 fps -> ~85 fps headless in gameplay (wgpu GS + AOT-compiled VU1).
- Web build: builds with `scripts/build_web.ps1 dmc`; serves with `scripts/serve.py`.

## What fixed the black screen

`ExitDeleteThread` cleared `g_current` and called `reschedule()`. With no other ready thread, `switch_to(nullptr)` returned early (`cur == next == nullptr`), so the deleted title task kept running as a "thread 0" zombie. It then called `Task_kill` on the next task, which had reused thread id 3. Fix in `runtime/src/kernel.cpp` `switch_to`. The `main` wait on `[0x5BEE18]` was normal: `Main_Get_Vsync` increments it every vblank.

## Open issues

- Web: Mission 1 is verified in the browser at ~60 fps with jumps landing. If Windows Defender quarantines `tools/emsdk/upstream/bin/wasm-opt.exe` again, web links fail with "binaryen executable not found"; restore it from Protection history.

- Web: the VU1-thread dips in heavy Mission 1 scenes are fixed (SIMD constants table in `vu_ops.hpp`, VIF fast paths, direct XGKICK); Mission 1 holds 60 fps headless with VU1 ~35% idle. Profiling and VU bench tools are described in ENGINEERING_LOG.md, 'VU1 thread saturation on the web'.
- Web: the movie-start stall is fixed (serve.py localhost/keep-alive + bounded CD waits).
- Not yet tested: save/load to the memory card, audio quality.

## Changes made

Session 2 (2026-10-19):

- `runtime/src/kernel.cpp`: `switch_to` goes idle when the current thread was deleted and nothing is ready (the black-screen fix). `kernel::dump()` prints COP0 status and pending INTC/DMAC. `PS2_TRACE_INTC=1` logs interrupt handler dispatch.
- `runtime/src/dispatch.cpp`: `PS2_PROBE_COUNT=<from>,<to>` counts probe hits per address.
- `runtime/src/dmac.cpp`: scratchpad DMA interleave mode (CHCR MOD 2, D_SQWC) for ch8/ch9. Fixes the Mission 1 ground/sea rainbow noise: the game builds a 256x256 texture from 16x16 tiles with interleaved fromSPR transfers, which were copied contiguously. Not a GIF path ordering bug. Details in `ENGINEERING_LOG.md`.
- `runtime/src/gs/raster.cpp`: `PS2_TRACE_TEXSUM=<frame>`, `PS2_TRACE_PRIM=f<frame>`, `PS2_TRACE_PRIM_BP=<hex>`, `PS2_SKIP_TBP=<hex>` (debug only).
- `CMakeLists.txt`: links the Windows system libraries wgpu-native needs.
- `third_party/wgpu-native`: v29.0.1.1 `windows-x86_64-gnu`.
- `tools/emsdk`: Emscripten 6.0.11.
- `scripts/build_web.ps1`: Windows version of `build_web.sh`.
- `generated/dmc/vu1_*.cpp`: 26 AOT-compiled VU1 programs, recorded in `games/dmc/vu1`.
- `games/dmc/disc/`: `SYSTEM.CNF` and `SLUS_202.16` for the web build.
- `games/dmc/run.sh`: boots through the title and alternates Start/Cross.

Session 1:

- `scripts/windows.py:35`
  - `windows()` now deduplicates aligned overlay bases with a `seen` set.
  - DMC ELF now yields exactly one `01f00000 4000` window instead of overlapping duplicates.
  - Verified with the DMC ELF, a synthetic ELF, and a synthetic RAM capture.

- `runtime/src/kernel.cpp:557`
  - `SleepThread` (`0x32`) now checks for a null `current()` thread before dereferencing it.
  - Pre-fix crash: access violation at frame 950, guest PC `0x00201F48`, native crash inside `ps2::kernel::handle_syscall`.
  - Post-fix: 1200- and 2400-frame runs complete.

- `runtime/src/iop/cdvdfsv.cpp:47-63`
  - Read jobs now complete both `0x80000595` and `0x80000596` (read and sync).
  - `kJobFsvComplete` also completes both calls.

- `runtime/src/iop/cdvdfsv.cpp:151`
  - `0x80000596` now replies immediately for non-sync methods and for sync when no read is pending.
  - Avoids holding `sceCdSync` forever when the read already finished.

- `runtime/src/iop/sif.cpp:803` and `runtime/src/iop/sif.hpp`
  - Added `sif::call_pending(id)` so `cdvdfsv` can ask whether a read call is still outstanding.

- `host/main.cpp:26,52`
  - Added a Windows unhandled-exception handler. It prints exception code, native offset, guest PC, guest RA, and frame.
  - This is diagnostic code and can stay until native bring-up is stable.

- `runtime/src/system.cpp:91`
  - Optional `PS2_TRACE_DISPLAY` logging in `dump_frame()`.
  - Logs display size, PMODE/SMODE2, display buffers, and sample pixels. Requires `--frames-dir` and `--frame-every`.

- `runtime/src/gs/gs.cpp`
  - `PS2_DUMP_VRAM` now accepts multiple `bp,bw,psm,w,h,path` regions separated by `;`.
  - `PS2_DUMP_VRAM` still writes at halt via `stats()`, so use it with a frame limit or a loaded state.

- `runtime/src/gs/raster.cpp`
  - Added optional `PS2_TRACE_PRIM=<start>` logging for primitive setup, TEX0 fields, scissor, and vertices.
  - Use a high start counter (for example `58000`) to avoid megabytes of boot logs.

No Git repository is present in this workspace. Changes are uncommitted and there is no commit hash to reference.

## Build and run

Run from the repository root.

### Build tools

```powershell
cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
ninja -C build/tools eerecomp vurecomp
```

### Translate DMC

```powershell
& 'build/tools/eerecomp.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --entries 'generated/dmc/entries.txt' --out 'generated/dmc'
```

### Build native runner

```powershell
cmake -S . -B build/native-dmc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang++ `
  "-DPS2_GENERATED=$((Get-Location).Path)/generated/dmc"
ninja -C build/native-dmc ps2run
```

### Run to the title

```powershell
& 'build/native-dmc/ps2run.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --disc '..\Devil May Cry.iso' `
  --memcard 'games/dmc/explore.ps2' `
  --frames 1200 `
  --press 300:start `
  --press 600:cross --press 660:cross --press 720:cross `
  --press 780:cross --press 840:cross --press 900:cross
```

### Run with diagnostics

```powershell
New-Item -ItemType Directory -Force -Path 'games/dmc/shots' | Out-Null
$env:PS2_TRACE_DISPLAY = '1'
$env:PS2_DUMP = (Join-Path (Get-Location) 'games/dmc/ram1200.bin')

& 'build/native-dmc/ps2run.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --disc '..\Devil May Cry.iso' `
  --memcard 'games/dmc/explore.ps2' `
  --frames 1200 `
  --frames-dir 'games/dmc/shots' --frame-every 300 `
  --misses 'games/dmc/misses.txt' `
  --press 300:start `
  --press 600:cross --press 660:cross --press 720:cross `
  --press 780:cross --press 840:cross --press 900:cross
```

`--frames-dir` does not create the directory. Create it first or no screenshots are written.

Save-state caveat: `--save-state N:file` is checked after the frame-limit halt. To save at frame 900, use `--frames 901`. Saving with `--frames 900` silently produces no file.

## Evidence to preserve

- `games/dmc/shots10/frame_00750.png`: DMC title.
- `games/dmc/shots10/frame_00900.png`: red fade.
- `games/dmc/shots10/frame_01200.png` and later: black.
- `games/dmc/shots12/`: 10-frame sweep; title at 710-940, black from 950 onward.
- `games/dmc/vram_fb0.ppm`, `vram_fb1.ppm`: GS display buffers at frame 1200.
- `games/dmc/vram_1500.ppm`, `vram_1800.ppm`, `vram_13D0.ppm`: texture/CLUT buffers.
- `games/dmc/run2400d.log`: full boot after both fixes; exits cleanly, no held sync call.
- `games/dmc/run2400d.log` thread dump: `main` at `0x0015B4D9`, thread 2 on semaphore 0.
- `games/dmc/run4800.log`: still black; IPU activity and GS stats.
- `games/dmc/run_hle1300.log`: no `cdvdfsv`/`fileio` activity after boot.
- `games/dmc/ram2400.bin`: 36 MiB RAM dump; overlay window empty.
- `games/dmc/precrash.ps2`, `after1200.ps2`, `after2400.ps2`: save states.
- `games/dmc/misses*.txt`: absent on the tested paths.

## Next actions

1. Play through Mission 1 in the browser; keep `serve.py --record games/dmc/vu1-web` on, then fold new programs in with `vurecomp games/dmc/vu1 generated/dmc` (copy them into `games/dmc/vu1` first) and rebuild.
2. Install SDL2 (MinGW development package) to get a native window and the threaded VU1/GS pipeline.
3. Test the memory card save/load and audio.

## Testing

- `python scripts/dmc_regress.py`: 9 regression cases (boot, options menu, shop cutscene, Mission 1 idle/jump/walk, castle hall, sealed door, upstairs gallery). They check crashes, missing code, landing and movement, and compare screenshots against `games/dmc/golden`. `--record` refreshes the golden frames; `PS2_GS=soft` runs it against the software renderer, which now matches.
- `python scripts/dmc_explore.py <state> <steps> --start-frame N`: greedy explorer that walks Dante into new places and reports crashes or missing code.
- `node scripts/webprobe.mjs <url> <seconds>` after `scripts/webprobe.ps1`: headless Chrome harness for the web build (fps, console, screenshots).
- `python scripts/audio_check.py <wav>` with `PS2_DUMP_AUDIO=<wav>`: audio sanity (clipping, dropouts, loudness, spectrogram). Boot, cutscene, gameplay and hall audio pass; voices use linear rather than the hardware's Gaussian interpolation.
- Not yet verified: memory card format/save through the game's save flow, and combat (enemies are story-triggered; the explorer did not meet any).

## Launcher

Double-click `Play Devil May Cry.bat` in the repository root. It opens a small window where you choose the disc image, memory card, resolution, fullscreen and widescreen, then **Play** (native window) or **Play in browser** (starts the local server and opens Chrome/Edge). **Controls...** remaps keys for both versions (`games/dmc/controls.txt`). In the native window, **F5** saves a state to `games/dmc/quicksave.ps2` (launcher sets `PS2_QUICKSAVE`; it can be loaded with `--load-state`). The native build (with `SDL2.dll` next to `ps2run.exe`) and the web build must exist first.

## Native window

```powershell
cmake -S . -B build/native-dmc "-DSDL2_DIR=$((Get-Location).Path)/third_party/SDL2-2.32.10/x86_64-w64-mingw32/lib/cmake/SDL2"
ninja -C build/native-dmc ps2run
Copy-Item third_party/SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll build/native-dmc/
& build/native-dmc/ps2run.exe 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' --disc '..\Devil May Cry.iso' --memcard games/dmc/play.ps2 --window
```

Keys match the web page (Enter = Start, Z = Cross, X = Circle, A = Square, S = Triangle/jump, arrows = D-pad and stick). F11 toggles fullscreen. `PS2_SCALE=2` or `3` raises the internal resolution.

## Web build

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_web.ps1 dmc
python scripts/serve.py build/web-dmc "..\Devil May Cry.iso" 8620 --record games/dmc/vu1-web
```

Open http://localhost:8620/ in Chrome or Edge. Keys: Enter = Start, Z = Cross, X = Circle, A = Square, S = Triangle, arrows = D-pad, I/J/K/L = left stick. The first build takes several minutes (65 MB of generated C++); a rebuild after a runtime change is quick.

## Fast iteration with save states

- `games/dmc/cut9000.ps2`: opening cutscene.
- `games/dmc/s16000.ps2`: just before Mission 1.
- `games/dmc/play17600.ps2`: Mission 1 gameplay.

They were saved after the kernel fix. Load with `--load-state`, and pass `--frames` as an absolute frame number.

## Risks and gotchas

- Save states created before the `cdvdfsv` fix can still contain a stale held `0x80000596` call. Reproduce on a fresh boot, not only from a state.
- `--trace-iop` logs are large (about 2 MB for 400 frames). Filter the output.
- The crash handler and `PS2_TRACE_DISPLAY` code are Windows/debug aids; they are harmless but should be reviewed before a release build.
- WSL Ubuntu lacks CMake/Ninja/compilers and cannot `sudo` non-interactively. The Windows clang path works; do not block on WSL.
- The DMC ISO, generated C++, and built binaries must not be published. This repository contains no game data.
- The ISO sits outside the repo: from the repo root, `..\Devil May Cry.iso`.

## Key paths

- `scripts/windows.py` - overlay window discovery and RAM capture.
- `runtime/src/kernel.cpp` - EE kernel HLE; `SleepThread` fix.
- `runtime/src/iop/cdvdfsv.cpp` - CD/DVD SIF servers; sync completion.
- `runtime/src/iop/sif.cpp` - SIF RPC, held calls, HLE servers.
- `runtime/src/ipu/ipu.cpp` - IPU MPEG decode.
- `runtime/src/gs/gs.cpp` - GS memory, display, image transfer.
- `runtime/src/gs/raster.cpp` - software rasterizer/triangle setup.
- `host/main.cpp` - native runner and crash handler.
- `runtime/src/system.cpp` - frame loop, screenshots, display trace.
- `generated/dmc/` - generated DMC code; do not hand-edit.
- `build/native-dmc/ps2run.exe` - native runner.
- `build/tools/eerecomp.exe`, `build/tools/vurecomp.exe` - recompilers.
- `games/dmc/` - local logs, screenshots, save states, RAM dumps.
