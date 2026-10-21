# Agent Instructions - PS2 Web Recomp (Devil May Cry target)

This file is for coding agents working in this workspace. Read `HANDOFF.md`, `ENGINEERING_LOG.md`, and `PROGRESS_PLAN.md` first.

## Mission

Port Devil May Cry (`SLUS_202.16`) to the web using this static-recompilation runtime.

Current milestone: native boot -> Mission 1 gameplay works, and the web build runs at 60 fps through the intro FMV. Web Mission 1 is verified at ~60 fps, and the native window works with SDL2. Next: memory card save/load, the FMV-start hitch on the web, and playing further into the game.

## Ground rules

- Do not hand-edit generated code in `generated/dmc`. Fix the recompiler/runtime or regenerate it.
- Do not commit the ISO, generated C++, screenshots, save states, or other game data.
- Do not commit at all unless the user explicitly asks.
- Keep the existing code style. Do not add comments unless they explain non-obvious hardware/runtime behavior.
- Prefer the Windows native build. WSL is not required and is not currently usable for builds.
- Do not publish generated code or binaries; they are translations of the game executable.

## Environment

- OS: Windows, PowerShell 5.1.
- Compiler: `clang++` 22.1.8, target `x86_64-w64-windows-gnu`.
- CMake: 3.31.12.
- Ninja: 1.13.2.
- ISO: `..\Devil May Cry.iso` from the repository root.
- Extracted boot ELF: `C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16`.
- The workspace is not a Git repository. Use the filesystem state as the source of truth.

## Build

Run from the repository root.

```powershell
cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
ninja -C build/tools eerecomp vurecomp

cmake -S . -B build/native-dmc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang++ `
  "-DPS2_GENERATED=$((Get-Location).Path)/generated/dmc"
ninja -C build/native-dmc ps2run
```

Regenerate DMC only when the recompiler or entry list changes:

```powershell
& 'build/tools/eerecomp.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --entries 'generated/dmc/entries.txt' --out 'generated/dmc'
```

Expected result: 253 code units, 256 files in `generated/dmc`, about 65.8 MB.

## Run and observe

Minimum boot-to-title run:

```powershell
& 'build/native-dmc/ps2run.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --disc '..\Devil May Cry.iso' `
  --memcard 'games/dmc/explore.ps2' `
  --frames 1200 `
  --press 300:start `
  --press 600:cross --press 660:cross --press 720:cross `
  --press 780:cross --press 840:cross --press 900:cross
```

Important runner behavior:

- Start at frame 300 advances past the unformatted memory-card warning. Start at frame 30 does not.
- `--frames-dir` requires the directory to already exist. Create it with `New-Item -ItemType Directory -Force`.
- `--save-state frame:file` is checked after the frame-limit halt. Use `--frames 901` to save at frame 900.
- `PS2_DUMP=<path>` writes EE RAM at halt (36 MiB / 37,748,736 bytes).
- `--misses <path>` writes unresolved guest addresses. Check this file after every exploratory run.
- `--trace-iop` is very large. Use it only for short, targeted runs and filter the log.

Check results:

```powershell
Get-Content 'games/dmc/run.log' | Select-Object -Last 20
Test-Path 'games/dmc/misses.txt'
```

Screenshots are PPM. Convert with Pillow when inspecting:

```powershell
python -c "from PIL import Image; import glob; [Image.open(p).save(p[:-4]+'.png') for p in glob.glob('games/dmc/shots/*.ppm')]"
```

## Debugging

### Native crash

The native runner has a Windows unhandled-exception handler in `host/main.cpp`. It prints:

```text
[crash] code ... off ... pc ... ra ... frame ...
```

Use the guest PC/RA to find the generated unit or runtime function. The previous crash was at guest PC `0x00201F48` in `ps2::kernel::handle_syscall`, caused by `SleepThread` dereferencing a null `current()`.

### Black screen / display

`runtime/src/system.cpp` supports optional display tracing:

```powershell
$env:PS2_TRACE_DISPLAY = '1'
```

Use it with `--frames-dir` and `--frame-every`; the trace is emitted from `dump_frame()`.

`PS2_DUMP_VRAM` writes regions at halt. It accepts `bp,bw,psm,w,h,path` specs separated by `;`:

```powershell
$env:PS2_DUMP_VRAM='0,8,0,512,448,games/dmc/fb0.ppm;700,8,0,512,448,games/dmc/fb1.ppm'
```

`PS2_TRACE_PRIM=<start>` logs primitive setup, TEX0 fields, scissor, and vertices. Use a high start counter to skip boot; otherwise the log is very large.

### Save states

Save states are useful for fast reproduction, but they can contain stale SIF held calls created before a runtime fix. If a fix changes SIF/CD behavior, validate on a fresh boot before trusting a saved state.

### Known false leads

- `ioman` #20/#21 are registered stubs returning `-1`; they are not missing.
- A `secrman` version mismatch does not affect HLE lookup. HLE is keyed by library name and function index; the version map only checks existence.
- `cdvdman` #51 is still absent, but it is not the current blocker.
- `[iop-tty] SPU:T/O` and the SIF "waiting for reply" logs appeared during boot but did not prevent reaching the title.
- The WSL toolchain failure is irrelevant to the working Windows native path.

## Current priority

1. Verify gameplay in the browser (`scripts/build_web.ps1 dmc`, `scripts/serve.py ... --record games/dmc/vu1-web`).
2. Keep VU1 programs AOT-compiled: record with `PS2_VU_RECORD=games/dmc/vu1`, then `vurecomp games/dmc/vu1 generated/dmc`, then re-run the CMake configure. An interpreted VU1 costs ~7x in gameplay.
3. Native window needs SDL2. Without it, `ps2run` is headless and single-threaded.

### Fixed false lead

The `main` wait on `[0x5BEE18]` is the normal per-frame vsync wait; `Main_Get_Vsync` increments it every vblank. The real black-screen bug was a zombie thread after `ExitDeleteThread` (fixed in `kernel.cpp` `switch_to`).

## Definition of done for the current milestone

- Done natively: boot -> title -> FMV -> cutscene -> Mission 1, from a fresh boot, with no crash, unhandled syscall or missing code.
- `0x1F00000` overlay sections are size 0; nothing to capture.
- Remaining: the same path in the browser; memory card save/load.

## Key files

- `HANDOFF.md` - current state and commands.
- `launcher/launcher.ps1` - Windows launcher (`Play Devil May Cry.bat`).
- `ENGINEERING_LOG.md` - chronological findings and evidence.
- `PROGRESS_PLAN.md` - phased plan and acceptance criteria.
- `scripts/windows.py` - overlay windows and capture.
- `runtime/src/kernel.cpp` - EE kernel HLE.
- `runtime/src/iop/sif.cpp` - SIF RPC and held calls.
- `runtime/src/iop/cdvdfsv.cpp` - CD/DVD SIF servers.
- `runtime/src/ipu/ipu.cpp` - IPU decode.
- `runtime/src/gs/gs.cpp` - GS memory/display/image transfers.
- `runtime/src/gs/raster.cpp` - software rasterizer.
- `host/main.cpp` - native runner/crash handler.
- `runtime/src/system.cpp` - frame loop/screenshots/display trace.
- `generated/dmc/` - generated code; do not edit.
