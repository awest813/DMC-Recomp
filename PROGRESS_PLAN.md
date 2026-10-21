# Progress Plan - Devil May Cry Web Port

Last updated: 2026-10-19

## Goal

Bring Devil May Cry (`SLUS_202.16`) from a disc image to a playable web build using static recompilation, without game-specific code where the generic runtime can be fixed instead.

## Current status

| Phase | Status | Summary |
|---|---|---|
| 0. Audit and plan | Done | ISO/ELF inspected; overlay model and runtime gaps identified. |
| 1. Native toolchain and translation | Done | Windows clang build; 253 DMC units generated. |
| 2. Boot to title | Done | Memory-card warning, Capcom logo, DMC title. |
| 3. Post-title transition | Done | Zombie thread after `ExitDeleteThread` fixed in `switch_to`; intro FMV and cutscenes play. |
| 4. Gameplay and overlays | Mostly done | Mission 1 playable headless (~frame 17000); no misses; `t_*` overlays are size 0. Ground/sea texture noise fixed (scratchpad DMA interleave). |
| 5. Web/Emscripten build | In progress | emsdk 6.0.11 in `tools/emsdk`; `scripts/build_web.ps1 dmc`. |
| 6. Validation and performance | In progress | wgpu-native + VU1 AOT: ~6 -> ~70-90 fps headless. |

## Phase 0 - Audit and plan

- [x] Identify boot executable and volume metadata.
- [x] Inventory overlay sections and group addresses.
- [x] Confirm no direct static seeds into `0x1F00000`.
- [x] Identify likely runtime gaps: IPU commands, IOP stubs, SIF, GS features.
- [x] Fix `scripts/windows.py` duplicate overlay-window bug.
- [x] Verify with DMC ELF and synthetic cases.

## Phase 1 - Native toolchain and translation

- [x] Build `eerecomp` and `vurecomp` with Windows clang.
- [x] Translate DMC main executable to `generated/dmc`.
- [x] Build `build/native-dmc/ps2run.exe`.
- [x] Verify generated output: 253 code units, 1,018,468 instructions.
- [x] Confirm no WSL dependency for native work.

Acceptance: tools and runner build cleanly; generated code links and starts.

## Phase 2 - Boot to title

- [x] Reach memory-card warning.
- [x] Advance with Start at frame 300.
- [x] Reach Capcom logo (~450).
- [x] Reach DMC title (~750).
- [x] Capture screenshots.
- [x] Confirm no missing-code file on boot/title path.
- [x] Save state at frame 900 for reproduction.

Acceptance: boot to title is repeatable from a fresh run.

## Phase 3 - Post-title transition (current)

- [x] Fix native access violation at frame 950 (`SleepThread` null dereference).
- [x] Fix held `0x80000596` CD sync call.
- [x] Confirm 1200- and 2400-frame runs exit cleanly.
- [x] Confirm IPU decodes during the transition.
- [x] Confirm display buffer is black at 512x448.
- [x] Confirm overlay window `0x1F00000` is empty at frame 2400.
- [x] Confirm black starts at frame 950 with a 10-frame screenshot sweep.
- [x] Dump/inspect GS VRAM at frame 1200.
- [x] Confirm only two untextured black sprites are drawn at frame 1200.
- [x] Locate the EE `main` wait at `0x0015B4D8` (`[0x5BEE18]` vs `[0x5BEFE8]`).
- [x] Find the producer of `[0x5BEE18]` (`Main_Get_Vsync`, fires every frame; the wait was normal).
- [x] Thread 2 is libkernel `topThread`; semaphore 0 wait is normal.
- [x] Root cause: deleted task thread kept running as a zombie and killed its successor task (kernel `switch_to`).
- [x] Reach the first playable gameplay frame.

Acceptance: transition shows visible movie/loading content and reaches gameplay without crash or missing code.

### Immediate next actions

1. Find writes to `0x5BEE18` and `0x5BEFE8`; trace the counter producer.
2. Inspect `main` (`0x0015B2C0`), `Main_Get_Vsync` (`0x0015B8D0`), `MainGsSwapDBuffDc` (`0x0015C010`), and `MainGsSwapDBuffDc_movie` (`0x0015C2A0`).
3. Trace the vblank handler and thread 2/semaphore 0 wakeup path.
4. Use `PS2_TRACE_PRIM`/`PS2_DUMP_VRAM` if GS output changes; at frame 1200 it does not.
5. Validate any fix from a fresh boot, not only from a save state.

## Phase 4 - Gameplay and overlays

- [x] Reach Mission 1 gameplay.
- [x] Overlay window `0x1F00000`: sections are size 0 and stay empty; nothing to capture.
- [x] No `misses*.txt` on boot -> Mission 1.
- [x] Record VU1 programs (`PS2_VU_RECORD`) and AOT-compile them with `vurecomp`.
- [x] Fix ground/sea texture noise (block `0x1400`; was missing scratchpad DMA interleave mode, not GIF ordering).
- [ ] Verify controls, memory card screen, and save/load path.

Acceptance: gameplay is reachable; overlays resolve; no missing-code file on the tested path.

## Phase 5 - Web/Emscripten build

- [x] Install Emscripten SDK 6.0.11 into `tools/emsdk`.
- [ ] Build the web target for DMC.
- [ ] Serve with `python scripts/serve.py ...`.
- [ ] Verify WebGPU/software fallback, audio, input, and save persistence.
- [ ] Verify disc streaming over HTTP Range requests.
- [ ] Check browser console for WASM/runtime errors.

Acceptance: DMC boots and reaches the same milestone in the browser as native.

## Phase 6 - Validation and performance

- [ ] Run native and web side-by-side for rendering differences.
- [ ] Measure frame time and GPU/CPU bottlenecks.
- [ ] Verify memory card import/export.
- [ ] Run a longer soak test.
- [ ] Document known limitations and unsupported features.

Acceptance: stable playable build with documented limits.

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Black screen is a deep IPU/GS presentation bug | Blocks gameplay and web validation | Dump GS VRAM, isolate texture vs display, compare with reference emulator |
| Black screen is a hidden load/streaming loop | Blocks progression | Trace fileio/CD completions around frames 1200-2400 |
| Save states contain stale SIF state | False reproductions | Validate fixes from a fresh boot |
| No Git repository | Hard to diff/revert | Keep logs; make small changes; document every patch |
| Generated assets are large | Slow builds and disk use | Regenerate only when needed; keep `build` and `games` local |
| Windows-only debug instrumentation | Portability/release risk | Guard with `_WIN32`; review before release |
| Web build not attempted | Unknown Emscripten/wgpu issues | Reserve a full phase; do not assume native success transfers |

## Definition of done

- Native: boot -> movie/transition -> gameplay -> first save.
- No access violations, unhandled syscalls, or missing-code reports on the tested path.
- Overlays captured and resolved.
- Web build reaches the same milestone with input, audio, and memory card support.
- Known limitations documented in `HANDOFF.md` and `ENGINEERING_LOG.md`.
