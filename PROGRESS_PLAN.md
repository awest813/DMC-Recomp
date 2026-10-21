# Progress Plan - Devil May Cry Port

Last updated: 2026-10-21

Goal: bring Devil May Cry (`SLUS_202.16`) from a disc image to a playable web build through static recompilation, fixing the generic runtime rather than adding game-specific code.

## Status

| Phase | Status | Summary |
|---|---|---|
| 0. Audit | Done | ELF, disc and overlay layout understood; the `t_*` overlay sections are size 0. |
| 1. Toolchain and translation | Done | Windows clang build; 253 EE units, 26 AOT VU1 programs. |
| 2. Boot to title | Done | Memory-card warning, Capcom logo, title menu. |
| 3. Title to gameplay | Done | Zombie-thread kernel fix; movie, cutscenes, prologue, Mission 1. |
| 4. Gameplay | Mostly done | Movement, jumps (VU0 flag fix), shooting, sword, menus, the castle hall and gallery. Combat and saving are not yet exercised. |
| 5. Web build | Done | Emscripten build, disc streaming, input, audio, 60 fps; software fallback without WebGPU. |
| 6. Validation and performance | In progress | Regression suite, explorer and audio checks in place; combat, card saving and SPU accuracy remain. |
| 7. Player experience | Done for now | Launcher, remappable controls, F5 quick-save, native window with SDL2. |

## Remaining checks

Phase 4 - Gameplay

- [ ] First enemy fight: rendering, effects, HUD gauge, hit reactions (from an F5 quick-save at the fight).
- [ ] Save at a save point on a fresh card (game-driven format), then **Load Data** from the title.
- [ ] Play a full mission to its results screen.

Phase 6 - Validation and performance

- [ ] Replace linear SPU2 interpolation with the hardware's Gaussian table (from a verified source).
- [ ] Compare the stabbing cutscene's fire patches with PCSX2.
- [ ] Longer soak test (an hour of play, native and web).
- [ ] Browser memory card persistence and export/import with real save data.
- [ ] Optional: speed up the software renderer (~10 fps in the browser without WebGPU).

## Done

- [x] Native crash in `SleepThread` at frame 950 (null current thread).
- [x] CD sync call held forever (`cdvdfsv` 0x80000596).
- [x] Black screen after the title: a deleted task thread kept running as a zombie and killed its successor (`kernel.cpp` `switch_to`).
- [x] Falling through the floor after jumps: VU0 macro ops did not set MAC/status flags (`vu.hpp`).
- [x] Ground/sea texture noise: scratchpad DMA interleave mode (`dmac.cpp`).
- [x] Software renderer speckles: float colors truncated just under the integer failed alpha tests (`raster.cpp`).
- [x] Speed: wgpu-native GS + AOT VU1 (~6 -> ~85 fps headless); VU1 thread fixes for the web (60 fps).
- [x] Web: movie-start stall (dev server IPv6/keep-alive, bounded CD waits); arrows drive the stick; GPU stat; no-WebGPU crash.
- [x] Windows: wgpu-native and SDL2 linking, HWND surface, `build_web.ps1`.
- [x] Controls verified against the game's Controller Setup screen; key remapping on both hosts.
- [x] Audio sanity at title, movie, cutscene, gameplay.
- [x] Regression suite (9 cases), explorer, headless browser probe, audio check.

## Definition of done

- Native and web: boot -> title -> movie -> gameplay -> first fight -> first save -> load, with no crash, unhandled syscall or missing code.
- Input, audio and memory card work in both, with known limitations documented in `HANDOFF.md`.

## Risks

| Risk | Mitigation |
|---|---|
| Later missions hit unimplemented IOP calls, GS features or VU programs | `--misses`, `[iop] missing` and `[sys] unhandled` in every run; `serve.py --record` collects new VU1 programs; the explorer for coverage. |
| Save states hold stale IOP state after a runtime fix | Validate fixes from a fresh boot. |
| Windows Defender quarantines emsdk's `wasm-opt.exe` | Restore it from Protection history; do not change security settings. |
| Golden frames hide an intended change | Refresh with `--record` only for intended output changes, and record why. |
