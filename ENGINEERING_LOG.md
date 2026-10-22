# Engineering Log - Devil May Cry Port

Target: `SLUS_202.16` / `Devil May Cry.iso`. Chronological record of findings, evidence and fixes; entries are not rewritten later, so early sections describe what was believed at the time. Current status and commands are in `HANDOFF.md`.

## Index

| Topic | Section |
|---|---|
| Audit, toolchain, overlay windows | 2026-10-19 - Initial audit and native bring-up |
| Crash at frame 950 (`SleepThread`) | Crash at frame 950 |
| CD sync call held forever | `cdvdfsv` sync hang |
| Black screen after the title (root cause: zombie thread) | Black screen after title (investigation) and 2026-10-19 (session 2) |
| GPU renderer, AOT VU1, performance table | 2026-10-19 (session 2) > Performance |
| Ground/sea texture noise (scratchpad DMA interleave) | 2026-10-19 (session 2) > Fixed: ground/sea texture noise |
| Falling through the floor (VU0 macro flags) | 2026-10-19 (session 2, later) - Falling through the floor |
| Native window, SDL2, HWND surface | 2026-10-20 - Native window on Windows |
| Web stalls, dev server, keyboard stick, GPU stat | 2026-10-20 - Web stalls, keyboard movement, GPU stats |
| VU1 thread speed on the web | 2026-10-20 - VU1 thread saturation on the web |
| Launcher, key remapping, controller layout, HUD | 2026-10-20 - Launcher, controls, HUD check |
| Regression suite, explorer, software renderer speckles | 2026-10-21 - Regression suite and explorer |
| No-WebGPU crash, doors | 2026-10-21 - Browser without WebGPU, door checks |
| F5 quick-save | 2026-10-21 - Quick-save in the native window |
| Audio check, memory card behavior | 2026-10-21 - Audio check, memory card behavior |

## 2026-10-19 - Initial audit and native bring-up

### Game and executable

- ISO: `..\Devil May Cry.iso`, 4,698,767,360 bytes.
- MD5: `1BD66D7C661EA9F1D8D9A0DD4EED6D40`.
- Volume: `DEVIL_MAY_CRY`, 2,294,320 sectors.
- Boot: `cdrom0:\SLUS_202.16;1`, `VER = 1.10`, `VMODE = NTSC`.
- PCSX2 CRC: `79B8A95F`.
- Boot ELF: `SLUS_202.16`, 6,688,460 bytes, entry `0x100008`.
- Main PT_LOAD: vaddr `0x100000`, filesz `0x47a500`, memsz `0x907b00`.
- 14 overlay code sections all at `0x1F00000`, size 0:
  `t_sce_at`, `t_rck`, `t_om_set`, `t_event`, `t_inf`, `t_scr_at`,
  `t_eff_db`, `t_mdview`, `t_light`, `t_c_main`, `t_cam`, `t_snd`,
  `t_flr`, `t_movie`; heap at `0x1F00100`.
- `_overlay_group_addresses` at `0x57a400`; starts `0x100000`, `0x1F00000`.
- Static scan found no direct jump/call seeds into `0x1F00000`; overlay discovery depends on runtime capture.

### Toolchain

- Windows native path works.
- `clang++ 22.1.8`, target `x86_64-w64-windows-gnu`, thread model `posix`.
- CMake 3.31.12; Ninja 1.13.2.
- WSL Ubuntu lacks CMake/Ninja/compilers and `sudo -n` requires a password. WSL is not used.

### Overlay window bug

`scripts/windows.py` treated duplicate aligned bases as consecutive windows. Fixed `windows()` to deduplicate with a `seen` set:

```python
def windows(elf):
    out = []
    seen = set()
    for v in load_phdrs(elf):
        if v[0] != 1 or v[4] or not (v[6] & 1):
            continue
        base = v[2] & ~(UNIT - 1)
        if base in seen:
            continue
        seen.add(base)
        out.append((base, UNIT))
    return out
```

Verification:

- DMC ELF: `01f00000 4000` (one window).
- Synthetic ELF with duplicate `0x1F00000`: duplicate collapsed; distinct `0x1F04000` preserved.
- Synthetic capture: `games/dmc/overlays/01F00000_AF2A2287.bin`, 65,536 bytes.

### Build and translation

Tools built:

```powershell
cmake -S . -B build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
ninja -C build/tools eerecomp vurecomp
```

Warning: `recomp/vurecomp.cpp:652` passes `std::filesystem::path::filename().c_str()` (`wchar_t` under MinGW) to `%s`.

Translation:

```powershell
& 'build/tools/eerecomp.exe' 'C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16' `
  --entries 'generated/dmc/entries.txt' --out 'generated/dmc'
```

Result:

```text
code 1018468 instructions (3978 KiB), functions 10111, entries 81146, rejected seeds 0
253 units written to generated/dmc
```

Native runner:

```powershell
cmake -S . -B build/native-dmc -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang++ `
  "-DPS2_GENERATED=$((Get-Location).Path)/generated/dmc"
ninja -C build/native-dmc ps2run
```

## Boot testing

### Memory card screen

First 300-frame run with Start at frame 30 stayed on:

> Memory card (8MB) inserted in MEMORY CARD slot 1 is not formatted.
> Press START button to begin game with default system setup.

Start at frame 300 advanced: Capcom logo around 450, DMC title around 600-750.

The card at `games/dmc/explore.ps2` starts unformatted. The runtime loads `IOPRP224.IMG` and the DMC IRX modules:

`SIO2MAN`, `PADMAN`, `MCMAN`, `MCSERV`, `LIBSD`, `SDRDRV`,
`MODHSYN`, `MODMIDI`, `MODMSIN`, `TSNDDRVM`, `CDMODULE`.

Recurring boot logs:

- `[iop-tty] SPU:T/O [wait (reset)]` twice.
- `[sif] call to 0x80000596 fn 1 is waiting for its reply`.
- `[sif] 0 packets wait for the EE (processed 1)`.

These did not prevent reaching the title on their own.

### Screenshots

- `games/dmc/shots/frame_00300.png`: memory-card warning.
- `games/dmc/shots3/frame_00450.png`: Capcom.
- `games/dmc/shots3/frame_00600.png`: DMC title.
- `games/dmc/shots10/frame_00750.png`: title menu.
- `games/dmc/shots10/frame_00900.png`: red fade.
- `games/dmc/shots10/frame_01200.png` onward: black.

## Crash at frame 950

### Symptom

A 1200-frame run with Start at 300 and repeated Cross presses exited with `-1073741819` (`0xC0000005` access violation). The log ended at:

```text
[io] write 0x1000F590 = 0x10000 (pc 0x0010A9A8)
```

That `[io]` line comes from `runtime/src/hw.cpp:39` and was not the root cause.

### Diagnosis

Added a Windows unhandled-exception handler to `host/main.cpp`. It printed:

```text
[crash] code 0xC0000005 ... pc 0x00201F48 ra 0x0019F46C frame 950
```

The native offset mapped to `ps2::kernel::handle_syscall`, not generated code. The guest PC `0x00201F48` is the `jr ra` after the `SleepThread` wrapper:

```text
L_00201F48:
    c.budget -= 2;
    { const std::uint32_t t = c.r[31].w[0]; c.pc = t; return; }
```

`kernel.cpp` case `0x32` (`SleepThread`) did:

```cpp
Thread *t = current();
ret(c, g_current);
if (t->wakeups > 0) { ... }
```

When `g_current` is `0` or not found, `current()` returns `nullptr` and `t->wakeups` faults.

### Fix

```cpp
case 0x32: {
    Thread *t = current();
    if (!t) { ret(c, -1); break; }
    ret(c, g_current);
    if (t->wakeups > 0) { --t->wakeups; break; }
    block_current(c, kWaitSleep, 0);
    break;
}
```

Result: 1200- and 2400-frame runs complete with exit `0`; no access violation.

## Save-state ordering

`--save-state 900:games/dmc/precrash.ps2 --frames 900` exited `0` but wrote no file. The vblank handler checks the frame limit before the save request:

```cpp
if (g_options.max_frames && g_frames >= g_options.max_frames) halt(...);
if (g_options.save_state_frame && g_frames == g_options.save_state_frame) g_state_request = true;
```

Use `--frames 901` to save at frame 900. This produced `precrash.ps2` (42,040,822 bytes).

## `cdvdfsv` sync hang

### Symptom

`[sif] call to 0x80000596 fn 1 is waiting for its reply` persisted. Server `0x80000596` is the CD sync server. The handler returned `false` for method 1, so `sif::call()` held the call in `g_held_calls`.

### Change

- Added `sif::call_pending(id)`.
- `0x80000596` now returns immediately for non-sync methods and for sync when `0x80000595` is not pending.
- Read jobs complete both `0x80000595` and `0x80000596`.

Result on a full boot: no `0x80000596` held-call message at halt. However, the black screen remained.

## Black screen after title

### Evidence

Full-boot 2400-frame run after both fixes:

- Exit `0`.
- No crash.
- No missing-code file.
- No held SIF call.
- IPU commands: `2:32256 3:64512 4:288248 5:8 6:3 7:35 9:3`.
- IPU output: 3,555,328 qwords.
- GS: 64,905 primitives at 1200; 72,107 at 4800.
- DMA: channel 2 and 3 active.

`PS2_TRACE_DISPLAY` at frame 1260:

```text
[gs] frame 1260 512x448 pmode FF66 smode2 3 fb0 0 fb1 1038 disp0 0 disp1 1BF9FF0203227C first FF000000 mid FF000000
```

So the display buffer is black with alpha.

At frame 2400, `0x1F00000` in `ram2400.bin` is all zero; no overlay was captured. The game is still running a per-frame IOP event/DMA loop and drawing about two primitives per frame, but the screen stays black.

### Frame-by-frame sweep

A 1200-frame run with `--frames-dir games/dmc/shots12 --frame-every 10` shows:

- Title/menu at frames 710-940.
- Fully black from frame 950 onward.
- No movie frame, loading text, or partial image appears.

This corrects the earlier estimate that black started around 1050.

### GS VRAM dump

Added semicolon-separated region support to `PS2_DUMP_VRAM` and dumped these at frame 1200:

```text
0,8,0,512,448       fb0 at 0x1000
700,8,0,512,448     fb1 at 0x1038
1500,1,0,64,128     texture buffer
13B8,1,0,16,16      CLUT-ish buffer
1800,1,0,64,256     texture buffer
13D0,1,0,16,16      CLUT-ish buffer
```

Results:

- `fb0` is all zero.
- `fb1` is black except for a small artifact region with bounding box `(16,320)-(432,402)`.
- `0x1500` and `0x1800` contain texture data; `0x13D0` contains a colorful 16x16 palette.
- At frame 1200, `PS2_TRACE_PRIM` shows only two sprites:
  - 512x224 at `(0,0)`, untextured, RGBA `00000000`, writing `fb1`.
  - 512x224 at `(0,0)`, untextured, RGBA `00000080`, z `0xFFFD`, ABE on, writing `fb1`.
- Textured PSMT4 tile sprites (`tbp 0x1500`, `tw/th 8`, `tcc 1`) exist earlier (the title/fade), but are absent at 1200.

### EE wait location

The full-boot halt log reports:

```text
[thread] 1 status 1 prio 1 wait 0/5 pc 0x0015B4D9 ra 0x0015B8BC
[thread] 2 status 4 prio 0 wait 2/0 pc 0x00202068 ra 0x00202DF0
[sema] 0 count 0 waiters 1
```

Symbols from `llvm-nm`:

```text
0015b2c0 T main
0015b8d0 T Main_Get_Vsync
0015b950 t Main_init
0015bd50 T Main_draw_clear
0015bdd0 T Main_draw_ot
0015c010 T MainGsSwapDBuffDc
0015c2a0 T MainGsSwapDBuffDc_movie
```

Thread 1 is inside `main` at `L_0015B4D8`:

```text
r2 = *(0x5BEE18)
r2 = (r2 < r3) ? 1 : 0
if (r2 != 0) goto L_0015B4D8
```

`r3` was loaded from `*(0x5BEFE8)`. In other words, `main` is busy-waiting for a counter at `0x5BEE18` to reach a target at `0x5BEFE8`. Thread 2 is blocked on semaphore 0.

A 1300-frame `--trace-iop-hle` run shows no `cdvdfsv` or `fileio` activity after boot. The game is not visibly requesting a new file during the black screen.

### Working hypotheses

> Superseded: none of these was the cause. The wait in `main` is normal; the black screen came from a zombie thread after `ExitDeleteThread`. See 2026-10-19 (session 2).

1. A counter used by `main` is supposed to be advanced by a vsync/field callback, movie thread, or DMA completion that the runtime does not deliver after frame 950.
2. The game is waiting for a movie/overlay thread (thread 2 / semaphore 0) that never receives its wakeup.
3. A stage-load path is blocked without issuing a file read, or the read path is not logged.
4. The movie is decoded but not presented. This is now weaker: by frame 1200 no full-screen textured primitive is submitted.

Hypothesis 4 was previously strongest; the GS dump and primitive trace move the lead toward an EE-side wait in `main`.

### Next experiment

- Find writes to `0x5BEE18` and `0x5BEFE8` and the producer of the counter.
- Inspect `main`, `Main_Get_Vsync`, `MainGsSwapDBuffDc`, and `MainGsSwapDBuffDc_movie` around the wait.
- Trace the vblank handler and thread 2 wakeup path; determine whether semaphore 0 should be released by the movie/load thread.
- If the counter is a frame/vsync counter, compare its expected progression with the runtime's vblank scheduling.
- Validate any fix from a fresh boot, not only from `after1200.ps2`.

## Corrigenda from the earlier audit

- `ioman` #20/#21 are registered stubs returning `-1` in `runtime/src/iop/io.cpp`; they are not missing.
- `cdvdman` #51 is still absent, but it was not the blocker on the boot/title path.
- `secrman` version mismatch does not affect HLE linking. HLE lookup is keyed by name and function index; the version map only checks existence.
- The WSL toolchain gap is real but irrelevant to the working Windows build.
- The initial 344-file disc listing and the runtime's 354-file count differ; reconcile only if file lookup fails.

## State at the end of session 1 (superseded)

- Fixed: overlay window duplication, native `SleepThread` crash, `cdvdfsv` sync hold.
- Working: native boot through title; screenshots; save states; 2400+ frame runs.
- Open: black screen from frame 950; EE `main` thread waiting on `0x5BEE18` vs `0x5BEFE8`; no gameplay reached; overlay capture empty.
- Not started: Emscripten/web build.
- No Git history exists in this workspace.

## 2026-10-19 (session 2) - Black screen fixed, gameplay reached, GPU + VU AOT

### The `main` "spin" was a red herring

- `Main_Get_Vsync` (`0x15B8D0`) is registered by `Main_init` through `sceGsSyncVCallback` and unconditionally increments `[0x5BEE18]`.
- `PS2_TRACE_INTC=1` (new, `runtime/src/kernel.cpp` `run_intc`) shows the handler firing every frame. `main` waiting at `0x15B4D8` is just its per-frame vsync wait; it was not stuck.
- At halt: COP0 status `0x70010C01` (interrupts on), INTC pending `0x4` (the vblank raised in the halting frame). `kernel::dump()` now prints this line.
- Thread 2 waiting on semaphore 0 is libkernel's `topThread`; normal.

### Root cause: zombie thread after `ExitDeleteThread`

Probed build (see below) showed the task scheduler goes empty after `Title_exit`. `--trace-syscalls` around frame 950:

```text
ExitDeleteThread ... thread 3
SignalSema/SleepThread/ExitDeleteThread ... thread 0   (x6)
CreateThread -> 3; StartThread 3; TerminateThread 3; DeleteThread 3
```

`exit_current(remove=true)` set `g_current = 0` and called `reschedule()`. With no other ready thread, `best_ready()` was null, and `switch_to()` returned early because `cur == next == nullptr`, so the deleted thread's code kept running as "thread 0". Its bookkeeping (`Task_chain`/`Task_kill`) then killed the newly created task, which reused id 3.

Fix (`runtime/src/kernel.cpp`, `switch_to`): only skip the switch when the current thread is real or the CPU is already idle:

```cpp
if (cur == next && (cur || (c.pc & ~kUnwind) == kIdle)) return;
```

Result: the intro FMV, the real-time opening cutscene, Mallet Island, and Mission 1 gameplay (Dante controllable, HUD, item menu) all run from a fresh boot. No missing code, no unhandled syscalls.

### Function-entry probe profiling

- `eerecomp --probes <file>` already emits `probe(c, pc)` at listed addresses.
- `probe()` (`runtime/src/dispatch.cpp`) now has a counting mode: `PS2_PROBE_COUNT=<from>,<to>` counts hits per probe between those frames and prints `[probe] 0xADDR N` at exit.
- Recipe: `llvm-nm -n SLUS_202.16` -> list of `T/t` symbol addresses -> `eerecomp --probes ... --out generated/dmc-probe` -> separate `build/native-dmc-probe`. `scripts/probediff.py a.log b.log` diffs two windows with symbol names.

### Performance

| Configuration | Speed (headless, cutscene/gameplay) |
|---|---|
| Software GS, VU1 interpreted | ~6 fps |
| wgpu GS (Intel Arc A750), VU1 interpreted | ~11.5 fps |
| wgpu GS, VU1 AOT-compiled (`vurecomp`) | ~70-90 fps |

- wgpu-native v29.0.1.1 `windows-x86_64-gnu` is in `third_party/wgpu-native`. `CMakeLists.txt` now links the Windows system libraries wgpu needs when `WIN32`.
- VU1 microprograms are recorded with `PS2_VU_RECORD=games/dmc/vu1` and compiled with `vurecomp games/dmc/vu1 generated/dmc` (produces `vu1_*.cpp`, `vu1_table.cpp`). Re-run the native CMake configure afterwards so the glob picks them up.
- Headless runs are single-threaded; the threaded VU1/GS pipeline is only used by the SDL window path (`host/window.cpp`). SDL2 is not installed here, so there is no native window yet.

### Fixed: ground/sea texture noise (scratchpad DMA interleave mode)

- Symptom: some ground/sea surfaces in Mission 1 showed rainbow noise, identical in the software and GPU renderers. `PS2_SKIP_TBP=1400` removed them: 257 triangles sample a 256x256 PSMCT32 texture at block `0x1400` (tbw 4).
- `0x1400` is also the shadow render target (512x224, cleared to `808080`, ~740 alpha-blended triangles), which made GIF path ordering look like the cause. It was not: the draw order is correct.
- Per frame the game uploads the 256x256 texture to `0x1400` itself, on PATH2 (VIF1 `DIRECT`, 16384 qwords) from main RAM at `0x014BEC80`, right before the 257 triangles draw. The uploaded RAM was already garbage at DMA time.
- That buffer is built by 256 fromSPR (ch8) transfers per frame with `CHCR = 0x108`: MOD 2 = interleave mode, QWC 64, MADR stepping by 0x40. Each moves one 16x16 tile: D_SQWC.TQWC qwords, then skip D_SQWC.SQWC qwords of RAM (one 256-pixel row), until QWC runs out.
- `dmac.cpp` treated MOD 2 as normal mode and copied each 1 KB tile contiguously, so tiles overlapped and the rest of the buffer kept old DMA packet data.
- Fix: `run_interleave()` in `runtime/src/dmac.cpp` for ch8/ch9 in MOD 2, matching PCSX2 `_SPR0interleave`/`_SPR1interleave`: block = TQWC (0 means all of QWC); MADR advances by block + SQWC after each block; the scratchpad side (SADR) is contiguous.
- Result: frame 17620 from `play17600.ps2` is clean in both renderers.
- Regression check, old vs new binary, software and wgpu GS: fresh boot to frame 1200 (every 60th frame) and `cut9000.ps2` to 9600 are pixel-identical. `s16000.ps2` (16030-16600) differs only where the sky behind the wall had the same blocky noise, now smooth. `play17600.ps2` to 18200 differs only in the formerly noisy ground/sea. No crashes or unhandled DMA messages.
- Debug aids still useful for texture glitches: `PS2_TRACE_DMA` (look for unusual CHCR modes), `PS2_TRACE_DMA1` (VIF1 chain tags), `PS2_WATCH=<addr>,<len>` (who writes a buffer).
- Debug aids in `runtime/src/gs/raster.cpp`: `PS2_TRACE_TEXSUM=<frame>` (unique texture/target setups per frame), `PS2_TRACE_PRIM=f<frame>` (log one frame), `PS2_TRACE_PRIM_BP=<hex>` (only prims touching that block), `PS2_SKIP_TBP=<hex>`.

### Overlays

The 14 `t_*` sections at `0x1F00000` have size 0. RAM at `0x1F00000` stays zero in gameplay and no misses are reported, so there is nothing to capture.

## 2026-10-19 (session 2, later) - Falling through the floor

### Symptom

Reported from the browser: Dante gets stuck in the terrain. Native repro from `games/dmc/ground17000.ps2` (Dante standing on the Mission 1 bridge): pressing jump (Triangle) makes him rise, come back down, and pass through the floor. `Pl_r1_escape_V` then keeps respawning him at the start point, where he falls through again forever. The camera ends up staring at the ground.

### Diagnosis

- Player work struct is at `0x5B71B0` (found by probing `Pl_r1_escape_V`'s `a0`). Position is at `+0x110` (x, y, z); gravity is -Y; the floor here is at y = -3527.
- `PS2_PEEK_FRAME=<hex addr>,<words>` (new, `runtime/src/system.cpp`) prints words as floats every vblank. `PS2_WATCH` was already taken by the write watchpoint.
- `PS2_PROBE_ONLY=<hex pc>` (new, `runtime/src/dispatch.cpp`) limits probe printing to one address.
- Airborne frames use `Pl_scr_hosei2` -> `Em_scr_at5` -> `Scr_Atari_4` -> `atari_check_4`. That function is VU0 macro-mode code which branches on the MAC flags (`cfc2 vi17`, masks `0x88`, `0x80`) after `vmul.x` and `vsub.w vf0, ...`.
- `runtime/include/ps2/vu.hpp` macro ops never updated `mac` or `status`, so `cfc2 vi17` returned stale values and the floor intersection test rejected every polygon.

### Fix

`vu.hpp`: FMAC ops (`add`/`sub`/`mul`/`madd`/`msub`, broadcast and accumulator forms, `opmula`/`opmsub`) set MAC and status with the same rule as micro mode (`vu_ops.hpp` `flags`). `max`/`mini`/`abs`/`move`/`mr32`/`ftoi`/`itof`/`mfir` leave the flags alone. Writes to `vf0` still update flags.

Result: jumps land. A 1600-frame walk/jump/attack session stays on the ground. A fresh boot to frame 17200 still plays the FMV, all cutscenes and Mission 1, with no misses.

### Renderer comparison

`PS2_GS=both PS2_GS_COMPARE=1` on cutscene and gameplay frames: ~5-13k pixels differ. The differences are sparkle speckles in the software reference renderer; the GPU output is clean. No GPU-side bug found.

### Web build blocked by Windows Defender

> Resolved 2026-10-20: the user restored `wasm-opt.exe`, and web builds work again. If it happens again, restore it the same way.

At 21:51 Defender quarantined `tools/emsdk/upstream/bin/wasm-opt.exe` (threat ID 2147731250, almost certainly a false positive). Every web link now fails with "binaryen executable not found". The failed link also left a new unoptimized `build/web-dmc/ps2web.wasm` beside the older `ps2web.js`, so the served page is broken until a build succeeds. Restore the file from Defender's protection history (or reinstall emsdk 6.0.11 with an exclusion for `tools/emsdk`), then re-run `scripts/build_web.ps1 dmc`.

## 2026-10-20 - Native window on Windows

- SDL2 2.32.10 MinGW development package in `third_party/SDL2-2.32.10` (from the official libsdl GitHub release). Configure with `-DSDL2_DIR=<repo>/third_party/SDL2-2.32.10/x86_64-w64-mingw32/lib/cmake/SDL2`, and copy `x86_64-w64-mingw32/bin/SDL2.dll` next to `ps2run.exe`.
- `host/window.cpp`: added an `SDL_SYSWM_WINDOWS` case (HINSTANCE/HWND). The POSIX `getrusage`/`sysconf` stats now have `_WIN32` versions (`GetProcessTimes`, `GetProcessMemoryInfo`; links `psapi`).
- `WindowTarget`/`SurfaceTarget` gained a `Windows` kind; `gpu.cpp` creates the surface from `WGPUSurfaceSourceWindowsHWND`.
- Result: `ps2run --window` presents through WebGPU (Intel Arc A750) with the threaded EE/VU1/GS pipeline, paced at 60 fps by the audio clock. 600 Mission 1 frames ran in ~11 s including startup.
- The web build was still blocked on 2026-10-20: Defender lists `wasm-opt.exe` as quarantined (ThreatStatusID 3) and the file is not on disk.
- 2026-10-20: after `wasm-opt.exe` was restored, `scripts/build_web.ps1 dmc` succeeded with all fixes (kernel thread, scratchpad DMA interleave, VU0 macro flags, 26 AOT VU1 programs).
- The web page can boot straight from a save state: copy it into `build/web-dmc/` and open `http://localhost:8620/?state=ground17000.ps2&press=17060:lup:200,17300:triangle:10`. Press frames are absolute guest frames.
- In the browser, Mission 1 from `ground17000.ps2` renders correctly, including the sea. Speed could not be measured because the browser pane was hidden (`document.visibilityState == "hidden"`), and Chrome throttles hidden pages to ~1 fps.
- 2026-10-20, with the pane visible: from `ground17000.ps2` the browser ran Mission 1 at 54-66 fps (~60 on screen), with the emulator thread ~61-67% busy, VU1 ~50% and GS ~75%. Walking, two jumps (frames 17300 and 17700) and shooting all played out; Dante lands and stays on the cliff path. Browser gameplay is verified.

## 2026-10-20 - Web stalls, keyboard movement, GPU stats

### Headless browser harness

The Claude browser pane is throttled to ~1 fps whenever the app window is in the background, which made timing runs unreliable. `scripts/webprobe.ps1` starts a throwaway headless Chrome (WebGPU on, background throttling off, profile under `%TEMP%`, DevTools on port 9333). `scripts/webprobe.mjs <url> <seconds> [--shot file] [--every ms]` loads the page, clicks Start, and prints the status line and console. `webprobe.ps1 -Stop` shuts it down. Pages take `?press=frame:button[:frames],...` and `?state=<file in build/web-dmc>`.

### Stall when the intro movie starts (web only)

- Native had no frame over 40 ms (`PS2_SPIKES=<ms>`, new, logs slow frames).
- In the browser the emulator thread went nearly idle during the stall: it was blocked, not computing. `?tracedisc` (new) logs chunk fetches over 50 ms: each 256 KB range took 50-180 ms from localhost.
- curl showed ~210 ms of *connect* time per request to `localhost`. Windows resolves `localhost` to `::1` first, `serve.py` only listened on 127.0.0.1, and it spoke HTTP/1.0, so every chunk paid a new connection plus the IPv6 fallback.
- Fix (`scripts/serve.py`): HTTP/1.1 keep-alive, and an extra `::1` listener when serving the default loopback address. Requests now take 2-9 ms.
- `cdmodule: Read Abort` also appeared only on the web: the CD read job polled `disc::ready()` every emulated ms while emulated time ran ahead of the download. `runtime/src/iop/cdvd.cpp` now polls for at most ~2 ms of emulated time, then blocks on the read. The wait log is behind `--trace-iop-hle`.
- Result: an 80 s fresh boot through the movie start stays at 57-60 fps, with two brief dips (45/50) and no Read Abort.

### Keyboard movement on the web

DMC only moves Dante with the left stick; the D-pad does nothing in gameplay (checked natively). The web page mapped arrows to the D-pad only, so the arrow keys could not move Dante. `web/shell.html` now drives the left stick from the arrows too, as the native window already did. Verified headless: holding ArrowUp walks Dante to the door.

### GPU % in the status line

`gpu::busy_ms()` returned average-pass-time x pass-count. One bogus timestamp sample inflated it, and it could decrease, which showed as `GPU 1210470%` or negative values. Samples over 50 ms are now ignored, and busy time accumulates per pass. Mission 1 shows GPU 15-39%.

### Higher internal resolution

Mission 1 and the stabbing cutscene render cleanly at `PS2_SCALE=2` and `3`. The cutscene shows hard-edged rectangular fire patches on Dante's body at every scale, including 1x. They may be the burning blade (correct) or a missing mask; unconfirmed without a hardware reference.

### Next performance target

> Resolved the same day: see 2026-10-20 - VU1 thread saturation on the web.

In heavy Mission 1 scenes the browser's VU1 thread reaches 0% idle (all 26 recorded programs are AOT-compiled; nothing new was recorded), and those are the frames that dip to 43-50 fps. Profiling VIF unpack + VU1 on the VU thread is the next step.

## 2026-10-20 - VU1 thread saturation on the web

### Measuring

- Before: from `ground17000.ps2` (`?press=17060:lup:200,17300:triangle:10`), headless runs varied a lot. The bad runs dipped to 39-55 fps with the VU and emulator threads at 0% idle; the good runs held 60 fps with VU 13-32% idle.
- `webprobe.mjs --profile <seconds> [--interval us]` (new) attaches the DevTools CPU profiler to every worker from that point on and prints each worker's hottest functions (the web build links with `--profiling-funcs`). On the VU worker: `vu1_6390BA9A810723A4` 37%, `vif::run` 13% (the inlined generic unpack loop), other VU1 programs ~14%, futex wait 24%.
- Native: `PS2_THREAD_PROFILE=<file>` (new, `host/thread_profile.cpp`, Windows window build only) samples the emulator and VU threads about once a millisecond; `scripts/thread_profile.py <ps2run.exe> <file> [top] [--thread vu] [--programs]` symbolizes it with llvm-nm. The VU thread was ~37% busy at 60 fps, two-thirds of that in AOT VU1 programs.
- `PS2_VU_CAPTURE=<file>:<skip>:<count>` (new, `vu_micro.cpp`) saves registers and VU memory before each compiled VU1 call. `recomp/vubench.cpp` replays a capture through the generated programs and prints the time per pass, per program, and a checksum of registers, VU memory and XGKICK data. `scripts/vubench.ps1 <capture>` regenerates the programs into `build/vubench/gen` and runs the bench natively and in Node (wasm SIMD). Use a forward-slash Windows path in the env var: Git Bash rewrites `/c/...:a:b` as a path list.

### Findings and fixes

- The hot VU1 program had 402 `v128.const` instructions in its wasm: LLVM rematerializes the `vfix` masks (0x7FFFFFFF, 0x7F7FFFFF, FLT_MIN, sign) at every use, and V8 expands each into several x64 instructions. `vu_ops.hpp` now reads all SIMD constants from a writable table (`g_vconst`, `vk()`), so each is loaded once into a local. The codegen emits `vk(kVf0)`/`vk(kOneSplat)` for vf0 operands. Bench, 20,000 Mission 1 calls: wasm 102 -> 75 ms per pass, native 85 -> 80 ms, checksum unchanged (`79687D6F6A7B0A22`).
- `vurecomp` keeps VF registers and ACC in locals (`local_vf`), written back at exits. No measurable gain on its own, but it removes per-instruction stores. `efu` takes the source register by value for this.
- VIF: `0x6F` (V4-5) and `0x65` in offset mode (STMOD 1) went through the generic per-element loop. The fast unpack templates now cover V4-5 and STMOD 1/2 (masked and fill-mode unpacks still use the generic loop). `vif::run` on the VU worker: 13% -> 3%.
- `xgkick` hands the packet straight from VU memory to `gif::transfer` unless it wraps, instead of copying it into a 16 KB stack buffer first.
- `generated/dmc/vu1_*.cpp` regenerated with `vurecomp games/dmc/vu1 generated/dmc` (26 programs).

### Result

- Browser (headless, three runs from `ground17000.ps2`, 30-45 s each): 60 fps throughout after load, VU thread 24-38% idle, emulator 32-46% idle. No dips below 57 fps on screen.
- Rendering: a baseline build of the old sources (`build/base`) and the new build ran `ground17000.ps2` to 18200 with walking, two jumps and an attack, `PS2_GS=both PS2_GS_COMPARE=1`, every 60th frame: all 20 frames are byte-identical and the GPU-vs-software difference counts match line for line.

### Remaining headroom

- The VU1 programs are now near their floor with exact clamping: each FMAC needs one or two `vfix` (about six SIMD ops each).
- On the emulator worker, IPU `decode_macroblock` (9-14%), VIF1 shadow posting in `vif::transfer` (~8%) and the scalar VU0 macro ops (`maddabc` etc., ~8% together) are the largest items.

## 2026-10-20 - Launcher, controls, HUD check

### Launcher

`Play Devil May Cry.bat` (repository root) starts `launcher/launcher.ps1`, a WinForms window that needs nothing beyond Windows PowerShell 5.1:

- Disc image and memory card pickers; resolution 1x/2x/3x; fullscreen; widescreen. Settings persist in `games/dmc/launcher.json`.
- **Play** starts `build/native-dmc/ps2run.exe --window` with `PS2_SCALE` and `PS2_KEYS`. Logs go to `games/dmc/launcher-game.log`.
- **Play in browser** starts `scripts/serve.py` (hidden; stopped when the launcher closes) unless port 8620 is already serving, then opens Chrome/Edge in app mode with `?PS2_SCALE=&aspect=&keys=`.
- **Controls...** edits `games/dmc/controls.txt`. A controller line polls XInput every 1.5 s. The cheat sheet uses the game's own TYPE A labels.
- Script switches for testing: `launcher.ps1 -Play native|browser [-DryRun]` (DryRun prints the command line or URL).

### Remappable keys

- Format (`controls.txt`): `input=key[,key]`; inputs are `cross circle square triangle l1 r1 l2 r2 l3 r3 start select up down left right` (D-pad), `lup ldown lleft lright` (left stick) and `rup rdown rleft rright` (right stick). Key names: letters, digits, `Up Down Left Right Enter Backspace Space Tab Escape LShift RShift LCtrl RCtrl LAlt`. A listed input replaces its defaults.
- Native: `host/window.cpp` `load_keys()` reads `PS2_KEYS` and warns about unknown inputs or keys. Web: `web/shell.html` reads `?keys=input:Key,Key;...`.
- New defaults on both: the right stick on T/F/G/H, used by DMC for devil arm select. Keyboard stick input now takes precedence over the pad's right stick, as it already did for the left stick.
- Verified: native warnings for bad entries; in headless Chrome `?keys=lup:T` walks Dante with T, and the full launcher string keeps arrows working at 60 fps.

### DMC's own control layout (Options > Controller Setup, TYPE A)

Triangle = action (jump), Circle = sword, Square/Cross = shoot (Cross also confirm/examine), R1 = aim, L1 = devil trigger, L2 = show map, Start = menu screen, Select = option, D-pad = move cursor, left stick = move, right stick = Alastor/Ifrit select. This matches what each pad bit does in our runs, so the pad mapping is right end to end. The D-pad does not move Dante.

### HUD hidden while exploring: by design

The vitality gauge was not drawn in Mission 1 exploration (a full-frame trace: 23,442 prims, no 2D HUD). `Gauge_trans`/`Life_gauge_trans` run every frame; `Life_gauge_move` (`0x3B6F70`) only shows the gauge (180-frame timer, `Gauge` struct at `0x9EE360`, bytes +141/+142) when battle/event flags are set (`0x754130` & `0x80000` or `0x1000`, `0x75243C` & 1, gauge bytes +372/+373, or mode 13). Exploration without enemies leaves it hidden. Not a bug.

### Open

- The big door on the Mission 1 bridge does not open with Cross/examine or by walking into it; unclear whether that is the intended route. The cliff path below the bridge (left stick down) leads on. (Later: the bridge is a prologue; the route runs down the cliff path to the tower door, where Mission 1 starts.)
- `PS2_TRACE_PRIM=f<frame>` now logs the whole frame (no 1000-prim cap); `PS2_TRACE_PRIM_TYPE=<n>` filters by primitive type.

## 2026-10-21 - Regression suite and explorer

### `scripts/dmc_regress.py`

One command: `python scripts/dmc_regress.py` (add `--record` to refresh the golden frames, `--only a,b` for a subset). Six cases cover a fresh boot to the title, the Options menu, the shop cutscene, Mission 1 idle, a jump that must land (`check_landed` on the player Y at `0x5B72C4`), and a walk that must move at least 300 units. Each case fails on: not reaching the frame limit, `[crash]`, unhandled syscalls, missing IOP imports, a non-empty misses file, failed position checks, or a screenshot that differs from `games/dmc/golden` by mean > 0.25 or p99 > 12 (channel units).

- Native GPU runs are bit-exact between runs (mean 0.00, p99 0), so the thresholds can be tight.
- Negative check: forcing the software renderer (`PS2_GS=soft`) fails `mission1-idle` (mean 0.66, p99 20).
- `PS2_FRAMES_ONLY=f1,f2` (new, `runtime/src/system.cpp`) limits `--frames-dir` dumps to the listed frames.
- Golden frames and the needed save states (`options1300.ps2`, `cut9000.ps2`, `ground17000.ps2`) are game output and stay local in `games/dmc`.

### `scripts/dmc_explore.py`

`python scripts/dmc_explore.py <state> <steps> --start-frame N` runs a greedy explorer. From each checkpoint it tries the four stick directions with and without jumping (4 runs in parallel), keeps the run that ends farthest from every earlier checkpoint, and continues from its saved state. Every run is checked for crashes, early halts and missing code, and frames go to `games/dmc/explore/step*/`. DMC's camera-relative controls flip direction at camera cuts, so manual steering is unreliable; this finds the way on through Mission 1 (bridge -> cliff path -> torch corridor -> stairs with a yellow orb -> tower bridge).
- The explorer went from the bridge (a prologue) through the cliff path, torch corridor, stairs and tower door to the "Mission 1: Curse of the Bloody Puppets - mission start" screen (needs Cross), then around the castle's grand hall: staircase, statue, the sealed red door, side corridors. 112 + 96 runs with no crash, early halt or missing code. No enemies were met; they are story-triggered.
- Save states for new areas: `games/dmc/hall21900.ps2` (inside the castle, just after mission start) and `games/dmc/hall22800.ps2` (by the sealed door). Both are regression cases (`castle-hall`, `castle-door`); the suite has 8 cases.

### Software renderer speckles (fixed)

Comparing the software renderer with the GPU in the castle hall showed thousands of dark speckles on lit walls and floors in the software output (19k differing pixels). `PS2_TRACE_PIXEL=x,y` (new, `runtime/src/gs/raster.cpp`; GS coordinates, so halve Y for interlaced 448-line output) logs every fragment at one pixel: texture, stride, filter, uv, color, blend factors, destination value and depth/alpha test state. Tracing one speckle showed the wall fragment rejected by the alpha test (ATST GEQUAL, AREF 128): its alpha was 127 because the vertex alpha (128 at every vertex) came out 127.9999 from float barycentrics and was truncated. The GS interpolates in fixed point, so constant vertex colors stay exact. `plot()` now adds 1/256 before truncating interpolated colors. Triangles are also interpolated from a fixed vertex order, so redrawing the same triangle in another order gives identical values; flat shading still takes its color from the provoking (last) vertex.

Result: the software renderer matches the GPU reference on every regression case (mean <= 0.15, p99 <= 1; it was mean 0.66, p99 20 in Mission 1). In the hall, 0 pixels now differ by more than 8. This is the renderer browsers without WebGPU use.

## 2026-10-21 - Browser without WebGPU, door checks

### No-WebGPU fallback crashed (fixed)

`node scripts/webprobe.mjs <url> <s> --nogpu` (new) hides `navigator.gpu` from the page before it loads. The page reported "software renderer" but never told the runtime, which then called `navigator.gpu.requestAdapter` (TypeError) and would wait forever for an adapter callback.

- `web/shell.html` `chooseGpu()`: sets `Module.ENV.PS2_GS = 'soft'` when WebGPU is missing or has no adapter.
- `runtime/src/gs/gpu/gpu.cpp` `init()`: on the web, returns false before any WebGPU call when `navigator.gpu` is absent.
- Result: the castle-door state runs on the software renderer through the 2D canvas copy at ~10 fps; the GS thread is the bottleneck (0% idle). That matches the README's expectation (fine for menus, slow for 3D). A one-off `requestAdapter` TypeError can still appear from WebGPU glue code; it does not stop the game.

### Doors

- Pressing Action (Triangle) at the sealed red door in the castle hall makes its seal flare up: door interaction works, and that door is sealed by design (the Mission 1 goal is to break the spell).
- The first explorer run stayed on the hall floor (height always 0) and never climbed the grand staircase. `dmc_explore.py` now weights height x4 in its novelty score; `--stop-at-battle` watches the battle flag (`0x754130` & `0x80000`, from `Life_gauge_move`) through `PS2_PEEK_WORDS=addr,...` (new, hex words logged every frame).
- With height weighted, the explorer climbed the grand staircase to the upper gallery (y = 3000): balconies, the angel statue with a blue flame, and carved wall reliefs. Another 192 runs with no crash or missing code, but no battle; the first fight needs a story trigger the explorer has not found. `games/dmc/upstairs28380.ps2` is regression case `castle-upstairs` (9 cases total).

## 2026-10-21 - Quick-save in the native window

F5 in the native window calls `ps2::request_save_state(path)` (new, thread-safe). The emulation thread takes it at its next vblank and saves at the usual safe point in `run()`. A save refused because the EE is inside an interrupt handler is retried (up to 1000 times) instead of dropped. The path comes from `PS2_QUICKSAVE` (the launcher sets `games/dmc/quicksave.ps2`), and `PS2_QUICKSAVE_AT=<frame>` triggers it for tests. Verified: a 2x threaded window run saved at frame 17200, and the file reloads headless at 1x with the position intact and a jump landing.

## 2026-10-21 - Audio check, memory card behavior

### Audio (`scripts/audio_check.py`)

`PS2_DUMP_AUDIO=file.wav` records the SPU2 output (48 kHz stereo) for a run. `python scripts/audio_check.py a.wav ... [--png dir]` prints duration, peak, clipped samples, DC offset, silent 50 ms windows, dropouts (a window 30 dB below two audible neighbours), L/R correlation, and a loudness strip; `--png` writes spectrograms. Clips recorded in `games/dmc/audio/` (game output, keep local):

| Clip | Result |
|---|---|
| boot (frames 0-1900) | Silent through the Capcom logo and warning; title music from frame ~775, fading with the red fade (900-975); intro-movie narration and music from ~1075. Peak -0.0 dBFS, no clipping, no dropouts. |
| shop cutscene (9000-9900) | Continuous, L/R correlation 0.86, no clipping or dropouts. |
| Mission 1 actions | Jump/sword/gun effects audible, no clipping or dropouts. |
| castle hall | Ambience plus footsteps, no dropouts. |

- Recording length matches emulated time exactly (500 frames = 8.33 s), so audio is locked to the game clock.
- Accuracy gap: voices use **linear** interpolation (`runtime/src/iop/spu2.cpp` `render_voice`); the hardware uses a 4-tap Gaussian filter (a 512-entry ROM table). SPU-synthesized clips carry more energy above 16 kHz (-32 dB) than the streamed movie audio (-45 dB), consistent with linear-interpolation imaging. Matching the hardware needs the exact table from a trusted source (psx-spx, or an emulator's copy), not a reconstruction.

### Memory card

The card is emulated at the page level (528-byte pages with ECC), and the disc's own MCMAN/MCSERV drive the filesystem. A new card is all 0xFF (erased, unformatted), as from a factory, which is why the boot warning appears. Changing an option (Vibration OFF) and exiting does not save with an unformatted card; the game keeps defaults, as its warning says. Formatting and saving should happen through the game's own save flow (PS2 games must offer to format); not yet exercised, since the first save point is beyond what the explorer reached.

## 2026-10-21 - Frame scheduling and GS destination-alpha accuracy

The frame/rendering audit is recorded in `docs/ACCURACY_AUDIT.md`. Two generic runtime fixes: vblank now advances from the scheduled deadline (also on state restoration), so late EE blocks cannot accumulate video-clock drift; both renderers ignore DATE on PSMCT24, which has no destination alpha bit. Existing regression captures remain byte-identical on the GPU; golden frames were not refreshed.

All nine game regressions pass on both GPU and software with the stricter RGB channel metric. Software's maximum mean error is 0.13 and p99 is 1 against the GPU goldens.

Added an optional disc-free `ps2_accuracy` CMake target and three CTest cases. It checks late PAL/NTSC field dispatch including save/load, and DATE color/depth behavior across 32/24/16/16S formats. GS checks pass on software and on the Arc A750 GPU at 1x/2x/3x. The screenshot regression metric now measures RGB channels equally instead of luminance; seven Python tests cover that metric and run validation. Missing states, nonzero exits and unknown case filters no longer produce misleading passes. Native and web builds succeed; the web link required a workspace-local temporary directory because the sandbox temporary directory denied writes.

## 2026-10-21 - GS 16-bit dithering

Implemented DIMX/DTHE in the software and compute renderers. The signed 3-bit matrix entries are added to RGB after blending and before final color clamp/wrap and 16-bit conversion, following the stage order in the PCSX2 software renderer. Alpha and 32/24-bit targets are unaffected. GPU setup records now carry both matrix halves; lookup uses native GS coordinates at 1x/2x/3x.

The disc-free rendering harness now has 6,180 assertions per backend, covering matrix position/repetition, signed values and ignored reserved bits, additive/subtractive blending, clamp/wrap, framebuffer masks and alpha preservation. Actual GS-register writes also switch DIMX and DTHE within a single GPU batch to exercise setup generation and record layout. Software and GPU at all three scales pass. All nine isolated GPU game regressions remain identical to the existing goldens; no goldens were refreshed.

Concurrent GPU/software regressions initially shared an output directory, deleting one GPU screenshot. Added `dmc_regress.py --out <directory>` and a Python check for it; the isolated GPU rerun passes all nine cases. Use separate output directories for parallel renderer runs.

Final validation: all nine software cases also pass (maximum mean 0.13, p99 1 against GPU goldens), eight Python tests pass, and the web build succeeds. A 20-second headless Chrome smoke test loaded the gameplay state at frame 17000 and reached 17926, presenting through WebGPU without console exceptions; final samples were 60 fps, with startup warmup and one transient dip. The sandbox blocked the localhost debugging connection, so the browser check ran outside the sandbox. No game output was committed or published.

## 2026-10-21 - SPU2 Gaussian voice interpolation

Replaced linear ADPCM voice resampling with the literal 512-entry hardware coefficient table documented by nocash. All 256 phase rows match the PCSX2 SPU2 table exactly. The filter uses pitch bits 4-11 and shifts each signed product before adding. Three prior decoded samples now follow each voice across block and loop boundaries and reset on key-on; streamed AutoDMA PCM bypasses the filter. The implementation and remaining audio gaps are described in `docs/AUDIO_ACCURACY.md`.

State format 4 stores the filter history separately, preserving the existing raw voice layout. Versions 2/3 still load, initializing absent history to zero. This may cause a short transition at a legacy state's block boundary; version 4 round trips preserve the output exactly.

Added `ps2_spu2_accuracy` to the optional accuracy targets and CTest: 10,457 assertions cover coefficients/phase vectors, rounding, both cores, pitch extremes, ADPCM loop boundaries, stereo gain, key-on reset, exact save/load, deterministic legacy migration, PCM bypass and sample counts. Four before/after game audio clips have identical sample counts and no clipping or detected dropouts. The movie portion after 20 seconds is byte-identical. Energy above 16 kHz drops by about 5 dB in the shop and 13 dB in the gameplay-actions clip; this supports the expected filtering change without claiming a hardware match. All nine game screenshot regressions pass unchanged.

Native and web builds succeed. The final headless Chrome smoke test loaded a version 3 gameplay state and reached frame 17954 without exceptions, ending at 60 fps with 58 ms queued audio after startup and transient dips. This validates browser execution/buffering; physical-device and long-session audio testing remain open.

## 2026-10-21 - Shared video rates and blanking

Added `ps2/video.hpp` with nominal profiles based on PCSX2's PS2-test-based counter model: NTSC/PAL interlaced, analog double-strike and 480p. Core field scheduling and native/browser fallback pacing share the same rate. Field deadlines carry fractional EE cycles through the saved field index; HBlank-clock counters and scanline alarms use rational conversions. Mode changes preserve integer counter values, reschedule counter targets and update SMODE2. Unsupported/custom video timings remain explicit fallbacks; see `docs/VIDEO_TIMING.md` for the profile values and limits.

EE/IOP blank ends are scheduled from the original blank-start deadline, using mode-dependent widths instead of 22 rounded lines. GS FIELD/VSINT changes occur at a separate delayed edge. State format 5 reserves the additional events, migrates versions 2-4 event IDs/alarms, and retimes old HBlank counters while preserving their legacy counts so target IRQs are not missed. The pending field boundary is retained when loading an old state.

Seven CTest targets pass, including five video profiles with 100,000-field sums, exact blank/GS edges, late dispatch, HBlank IRQs, mode switches, forced-region behavior, pending-GS save/load and legacy migration, plus the GS and SPU2 tests. Old/new screenshots for all eleven captures across nine cases were reviewed. Goldens were intentionally refreshed for changed field/animation sampling, with `games/dmc/golden-before-video-timing` preserving the prior set locally. Four audio captures at the new rate have no clipping/dropouts; sample counts scale with field duration while output stays at 48 kHz.

Strengthened pending-edge restoration by advancing past blank end before reloading the saved pre-GS-edge state. This exposed the unsaved guest-visible INTC latch; version 5 now stores it, with legacy snapshots initializing it from pending status. All seven CTest targets pass, including the rewind check. Older HBlank counter targets are also rescheduled from preserved counts during migration.

Final GPU game rerun: 9/9, all screenshots identical to the refreshed timing goldens. Native/web builds succeed. Headless Chrome loaded a version 3 state and reached frame 18011 without exceptions; final status was 60 fps with 79 ms queued audio, after startup and a transient dip. The full software suite passed before the final latch-persistence change; the final software recheck covers the other eight cases, while the long cutscene is covered by GPU and the independent state-edge tests.

The final targeted software rerun passed 8/8, with maximum mean 0.12 and p99 1 against the reviewed timing goldens. The final code has 7/7 CTest targets, 9/9 GPU game cases, and the Chrome smoke check passing. The earlier complete software run was 9/9; the long cutscene was not repeated after the final latch-only adjustment.

## 2026-10-21 - Preview, mipmaps and recurring GPU speckles

Opened a persistent localhost preview on port 8620. The final pending software shop check from the timing work passed (mean 0.07/0.13, p99 1), completing coverage of all nine timing cases on the final latch code.

The user reported sparkling stone/bridge textures and supplied a torch-corridor screenshot, then pointed out an earlier agent had fixed this. The existing log's "Software renderer speckles (fixed)" section identifies constant alpha rounding below AREF 128 and order-dependent interpolation. That protection existed only in the software path. GPU triangles now use the same canonical vertex order, preserving the original flat-shading provoking vertex, and GPU triangle/line attributes use an anchor plus differences so constant colors and Q remain exact. Reproduced the user's corridor from `games/dmc/explore/step04_ldown.ps2`; the browser view at 3x no longer shows the regular bright/dark stipple pattern. The preview remains available as `?state=sparkle-corridor.ps2` (local copied checkpoint only).

Separately, the bridge primitive trace exposed active explicit mipmaps: TEX1 FFFFFF3D00000124, nonzero MIPTBP1, and positive LOD on many vertices. Both renderers had ignored mipmaps entirely. Implemented explicit levels 0-6, signed K and dynamic Q/constant LOD, min/mag filtering, nearest mip selection and trilinear blending, coordinate/dimension/region scaling, and GPU mip-range hazards. Added TEX1/mip registers to primitive traces. Host floating-point LOD/filter weights and absent automatic MTBA address generation remain limitations.

Seven CTest targets pass. The GS target has 6,307 software checks and 6,308 GPU checks, including 120 mip combinations, six spatial filter ramps, constant-alpha and reordered coplanar passes across every scale layer. Final GPU checks passed at 1x/3x; initial mip/ramp checks also passed at 2x. Native/web builds pass. A preview navigation during linking briefly mixed old JS and new wasm; reloading after the completed build resolved it.

Full GPU and software game runs reached every frame limit with no runtime/check failures, but five screenshot cases correctly differed from the old level-zero goldens. Reviewed all eleven old/new captures and backed up goldens to `games/dmc/golden-before-mipmapping` before intentionally recording the new set. The record run passes 9/9. The five changed software captures compare against final GPU images with maximum mean 0.091 and p99 1. The unchanged software shop capture remains mean 0.13/p99 1.
Final independent GPU verification against the refreshed goldens passes 9/9, all eleven captures byte-identical. No additional reference refresh was needed.

## 2026-10-21 - SPU2 per-voice dry/wet routing

The VMIXL/VMIXR/VMIXEL/VMIXER masks were stored in the register shadow but ignored by rendering. Each of 24 voices now contributes independently to four dry/wet channel sums on each core. Muting a bus does not stop decoding, envelope/volume advancement or raw voice capture. Each voice sum saturates to signed 16-bit before master gain, and wet capture rings now contain the routed pre-MMIX voice samples instead of zeros. Register addresses, bus saturation/stage order and initial masks were checked against PCSX2's SPU2 register map, mixer and reference initialization. See `docs/AUDIO_ACCURACY.md` for links and limits.

No state version/layout change: all four masks were already saved in `Core::regs`; loaded masks are used directly. Reset initializes the 24 valid bits of each mask to the reference defaults.

The SPU2 test target now has 35,431 assertions, including both cores; voices 0, 15, 16 and 23; all 16 gate combinations; stereo output and all four voice capture buses; muted progression; saved routed output; wet-ring IRQA; bus saturation; and raw voice capture. Seven CTest targets pass. All nine GPU game regressions pass with byte-identical images and no golden refresh.

Four mixer WAV captures remain local in `games/dmc/audio-accuracy/voice-routing`: fresh boot/title/movie (1,520,704 stereo frames), shop (481,280), gameplay jump/gun/sword (321,120), and castle hall (481,280), all at 48 kHz. Every sample is byte-identical to the prior video-timing capture set, with zero clipped samples or detected dropouts. DMC's tested routing therefore remains audible exactly as before; synthetic tests establish the newly functional bus controls. This does not validate physical audio-device output or establish hardware-exact reverb.

Remaining: MMIX input/external dry/wet gates and the actual core-to-core output chain, reverb processing/return, voice noise/pitch modulation, and detailed ADPCM/ADSR/sweep timing. The existing PCM and host-output paths were preserved.

The voice-routing web build passed. A separate 20-second headless Chrome run loaded frame 17000 and reached 17772 without console exceptions, ending at about 60 fps with 73 ms queued audio. Startup showed repeated near-empty queues before stabilizing; the user's live preview was left alone.

### Browser scratchy startup report

The user reported very scratchy sound at the beginning. Inspection found immediate consumption of any producer samples, exponential holding/decay on starvation, and no fade when PCM resumed. Added 60 ms startup/recovery prebuffering and 5 ms fades to silence/back in, with unsigned queue distance for counter wrap. The host's 80 ms pacing target and the emulated PCM are unchanged. This targets audible discontinuities from warmup starvation; sustained inability to run in real time can still cause pauses.

Added `scripts/test_web_audio.mjs`, which executes the actual page worklet against a deterministic producer and tests silent prebuffering, preserved queue contents, unchanged steady samples, partial-quantum starvation/fade, recovery, stereo, active pacing and 32-bit wrap. Tests pass. Browser rebuild/playback validation follows below.
The startup-buffering web rebuild passed. The final 20-second headless check reached frame 17877 without console exceptions and ended with 74 ms queued audio; startup and competing preview work still caused transient frame-rate dips. The visible 3x corridor preview was reloaded and started with the new worklet. Physical listening remains the user-facing check; buffering/fades cannot prevent pauses under sustained real-time overload.

## 2026-10-22 - Core audio routing and launcher overhaul

Implemented MMIX dry voice/AutoDMA/external channel gates and the core 0 -> external gain -> core 1 signal path, replacing the host-side sum of two independent outputs. Sources saturate before each master gain; core 0 capture rings retain the pre-external-gain signal. Signed external gains, all dry channel gates and both cores are tested. Existing saved MMIX/gain fields are used directly; no state-version change. Wet MMIX routing/reverb, special mute/SPDIF paths and master sweeps remain open.

37,479 SPU2 assertions and all seven CTest targets pass. Nine GPU game regressions pass with identical screenshots and no golden refresh. Native/web builds pass. Four local 48 kHz captures in `games/dmc/audio-accuracy/core-routing` retain their expected sample counts and show zero clipping/dropouts; the extra reference gain stages change at most 3 sample levels and under 0.0015 dB RMS versus the prior routing captures.

Reworked the launcher into Play, Settings, and Controls & help pages, with a resizable DPI-scaled form, setup status, a main Start action, mode selection, timestamped native/browser quick-save resume, clear picture presets, a scrolling keyboard editor with validation/defaults/cancel, and direct access to save folders/logs. Existing settings/controls are retained. Window cards are explicitly distinguished from browser storage, and the original localhost origin is retained so browser cards do not disappear.

Window launch now starts ps2run directly instead of composing a cmd.exe command. Dry runs/snapshots no longer write settings/controls or start servers. Browser startup verifies a loopback server identity endpoint against the selected build and disc, and only owned servers are stopped on exit. Closing an owned server prompts before disrupting the browser game. The previously owned preview server was verified by exact PID/command and refreshed on the same address for the new identity endpoint.

Validated all three launcher pages, the scrolling controls dialog and missing-disc state with built-in snapshots. Visual review found and fixed initially offscreen Browse buttons caused by inactive tab default sizes. `scripts/test_launcher.ps1` covers dry-run isolation, literal bracketed/metacharacter paths, missing-disc readiness, direct native launch, quick-save selection, matching-server reuse and refusal of a different disc, and server ownership. `scripts/test_serve.py` passes identity/range/isolation-header checks on a tiny fixture. Native process starts are mocked in launcher tests; full gameplay is covered by the runtime regression suite. The browser audio worklet tests still pass.
Final 20-second browser playback check reached frame 18025 without console exceptions, ending near 59 fps with 54 ms queued audio after warmup. The redesigned launcher was opened as a visible native window (verified window title/handle). Its first review process stayed hidden because of the outer Start-Process window flag; relaunching with the same console-hiding PowerShell argument used by the existing batch file showed the form correctly.

## 2026-10-22 - Browser player overhaul

Replaced the browser shell's crowded toolbar with a ready screen, framed game view, concise session status, sound toggle, controls guide and settings dialog. Settings groups live volume, pending picture changes, memory-card backups and optional performance details. Picture changes require an explicit restart and preserve checkpoint/query options. Current keyboard mappings appear in the controls guide. Fullscreen uses Alt+Enter instead of F, which conflicted with the right stick, and includes accessible settings/exit buttons during play.

Opening panels or losing focus releases held input; panels suppress controller/keyboard/touch game input without claiming to pause emulation. Storage errors remain visible, including over modal dialogs, and imported cards reload only after a successful browser-storage write. Audio output uses a persistent volume/mute preference through a GainNode; emulated samples and the existing prebuffer/fades remain unchanged. Failed audio startup can continue muted and retry. The web pacer checks active audio inside its queue wait so failed/retried audio initialization cannot leave it waiting forever.

Web build passed. `scripts/test_web_audio.mjs` passed unchanged sample/buffering checks. New `scripts/test_web_ui.mjs` exercised the real built player in isolated headless Chrome: ready state, modal dialogs, portrait/landscape phone-sized bounds, pending picture settings, volume, mute, fullscreen, F-key input, input isolation and continuing gameplay while a panel is open. No JavaScript exceptions occurred. All nine native game regressions passed with identical frames; no golden refresh. The visible corridor preview was reloaded and started with the new shell. Ready/settings screens were visually reviewed; local captures remain untracked.

## 2026-10-22 - Audit and polish of pending changes

Reviewed the pending audio, launcher, browser, server, tests and documentation together. The SPU2 dry gates, saturation order, capture placement and signed core-to-core gain match the linked PCSX2 reference model; the runtime mixer required no further changes. Corrected an outdated assertion count in the audio document.

Fixed browser edge cases: audio initialization is serialized, retries recover audibly rather than immediately muting again, audio suspension releases host pacing, and toolbar actions restore game keyboard focus. Modal panels neutralize controller input while preserving player 2's connection and stopping rumble. The controls guide includes the complete launcher keyboard layout. Short portrait touch layouts can scroll, and the volume label has explicit spacing. Storage flushes are serialized and retain import callbacks across autosave debouncing; file-read errors are surfaced. Synchronous browser-storage initialization errors release their loading dependency, with disc preparation registered first so runtime initialization cannot occur early.

Launcher paths/settings/controls use literal filenames, relative disc/card paths are resolved before child processes change working directory, and both new/reused servers are verified through the same localhost host used by the browser. Refused sessions cannot overwrite the served quick-save snapshot. Failed owned-server startup is cleaned up, and settings/control write failures show actionable errors. The local server rejects ranges beyond EOF or with reversed bounds, decodes escaped filenames, and confines served build files to their real build directory.

Validation: all seven CTests, 37,479 SPU2 assertions, eight regression-harness tests, expanded mocked launcher launch/ownership checks, local server integration tests, worklet checks, startup failure checks and final real-runtime headless browser interaction tests passed. Browser coverage includes injected audio-device failure/retry, audio suspend/resume with continuing gameplay, storage callback serialization/error, controller connection preservation, toolbar focus, fullscreen, responsive bounds and live input. The final web build passed. Nine fresh native game regressions produced identical frames without a golden refresh. Launcher settings and portrait/landscape browser settings were visually reviewed. Preview server identity/PID were verified before refreshing it. Physical-device listening, first in-game memory-card save/load and combat remain outside these checks. All changes remain uncommitted.

## 2026-10-22 - Native pacing, controls, persistence and profiling

The SDL host previously consumed sound immediately, waited indefinitely for queued audio to drain, and released fields at the audio backend's coarse block boundaries. Added 60 ms startup/recovery prebuffering, 5 ms fade-in, a bounded device-drain watchdog and continuous video-mode deadlines with low-pass audio queue correction. Existing emulated video rates/blanking remain unchanged. Keyboard/controller input is neutral while unfocused, sticks have a rescaled deadzone, opposing stick keys cancel, and render-thread SDL polling is limited to 250 Hz. The pacing diagnostic logs actual field rates, interval percentiles, queue depth and underruns. The render thread now participates in sampling profiles.

Checkpoints/cards use same-directory staged file replacement after checked close. Failed card writes retain dirty data and retry after the quiet period; orderly exit forces outstanding writes. F5 completion/failure is reported in the window title. Requests wait for a safe kernel state before allocating/serializing, avoiding repeated full serialization inside interrupt handlers; terminal I/O failures are not retried 1,000 times. Snapshot writers reserve capacity to avoid repeated buffer growth. State formats are unchanged and launcher resume remains the native load flow.

Follow-up input audit found that restored held buttons could remain stuck behind the host's unchanged-state cache, and native host writes raced guest pad reads/serialization. Input now publishes buttons/axes/pressure together under a controller lock; native loads immediately republish live input under a host publication lock. Unchanged state is refreshed on each host poll, covering controller resets. Host-visible frame counters and shutdown state are atomic. Mapping files trim whitespace, and invalid key names preserve previous bindings. New native tests cover held-button restoration, 1,000 coherent concurrent controller packets, remapping, deadzones, focus, dummy-device pacing, atomic saves and SIO2 card persistence/retry. Fresh-process card reads are exercised separately.

The initial ordinary p95 field interval improved from about 25–30 ms to 18–19 ms. A one-minute 2x threaded window check ran near 59.94 fields/s after warmup, with one recovery at the quick save and no additional underruns. That snapshot interval was about 112 ms; quick-saving still pauses emulation. SDL's render-thread sample share was 17.7% before polling changes and 11.3% in a later profile, with differing background load, so this is not a controlled CPU speedup claim. The remaining sampled work is mostly EE/VU code, IPU decoding and GPU submission, with substantial waiting on each thread.

Native builds and all eight CTests pass. Repeated real threaded F5/resume checks yield a byte-identical final frame versus uninterrupted execution and refuse malformed checkpoints. Nine game regressions pass with identical frames and no golden refresh, including after the synchronization fixes. Worklet/startup tests still pass. See `docs/NATIVE_STABILITY.md` for the test commands and remaining physical-device/in-game-save validation limits. No game data or captures are tracked, and no commit was requested.

Final native-host target: 1,037/1,037 checks passed, including whitespace/invalid mapping behavior. The final shared-runtime web build and isolated real-runtime browser interaction/audio smoke check also passed after the controller and cross-thread status changes.
