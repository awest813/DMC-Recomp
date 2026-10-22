# Video timing - 2026-10-21

The core and both host fallback pacers now use one BIOS-mode timing profile. This replaces the core's exact 60/50 field rates, the native fallback's separate 60/50 constants, and the browser fallback's separate 59.94/50 constants. Audio-driven pacing remains driven by the audio device; the emulated field duration determines how much audio is generated per field.

## Reference and scope

The profiles follow the active timing formulas in [PCSX2 Counters.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Counters.cpp), which documents PS2 test results for blanking and separates EE vblank from the later GS FIELD/CSR update. Scanline totals are cross-checked against [PCSX2 Counters.h](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Counters.h). These are nominal reference-emulator timings, not new measurements of a console's oscillator.

| BIOS mode | Fields/periods per second | Lines per period | EE vblank width, lines | GS update delay, lines |
|---|---:|---:|---:|---:|
| NTSC interlaced | 59.94 | 262.5 | 22.5 | 3.5 |
| PAL interlaced | 50.00 | 312.5 | 24.5 | 3 |
| NTSC double-strike (240p-style) | 59.83 | 263 | 23 | 4 |
| PAL double-strike (288p-style) | 49.76 | 314 | 26 | 4.5 |
| 480p (`SetGsCrt` mode 0x50) | 59.94 | 525 | 45 | 7 |

The non-interlaced NTSC value follows PCSX2's active `59.94 - 0.11` calculation. A nearby source comment rounds it differently; this implementation does not treat that comment as an independently measured exact rate. Likewise, the NTSC profile uses nominal 59.94 rather than silently assuming an exact 60000/1001 crystal ratio.

Unsupported BIOS modes log that they use the existing analog-region fallback. Arbitrary SMODE1/SYNCH/SYNCV programming, HDTV/VESA timings, precise sub-scanline timer phase across mode changes, and timer gate semantics remain outside this change. Custom raw timing registers do not yet recompute the profile. The `--pal`/`--ntsc` override still takes precedence over region selection.

## Scheduling

- Field deadlines advance by rational EE-cycle intervals. Fractional cycles carry through the field index rather than being truncated once per field. The index is already saved in states, so loading retains the phase. EE dispatch lateness does not move future deadlines.
- EE and IOP vblank ends use the same mode-dependent deadline, measured from the scheduled blank start. The GS FIELD/VSINT update is a separate delayed event. Non-interlaced profiles hold FIELD constant.
- HBlank-clock counters and scanline alarms use rational scanline conversions instead of repeatedly dividing by a rounded integer line duration. BIOS mode changes preserve the integer counter value and reschedule its target; fractional sub-line phase is not retained across a mode change.
- A mode change affects intervals scheduled after the currently pending field boundary. Pending blank-end/GS-update events keep their original deadlines.
- `SetGsCrt` also updates the SMODE2 interlace/frame bits used by presentation.

State format 5 reserves explicit GS-update and IOP-blank-end event IDs and stores the guest-visible INTC interrupt latch. Versions 2-4 still load: the former out-of-range IOP event is mapped to its reserved ID, and alarm event IDs are adjusted for the new hardware-event range. Active HBlank counters preserve their legacy integer count and reschedule their targets with the new line duration, avoiding a missed target IRQ. The missing legacy interrupt latch is initialized from the saved pending interrupt status. An older state's already pending field deadline is retained; later fields use the new timing model. Version 4's Gaussian audio history is preserved.

## Validation and golden frames

Five timing CTest cases exercise the profiles above, including 100,000-field sums, exact blank/GS edges, deliberate EE budget overshoot, rational scanline edges, HBlank target interrupts, mode changes, save/load with pending GS work, and legacy event-ID migration. The restore test advances beyond blank end before rewinding to a pre-GS-edge save, verifying that future interrupt/FIELD state is cleared. The GS and SPU2 tests remain enabled.

Four local audio captures were repeated at the new NTSC rate with no clipping or detected dropouts. The 601-field shop/hall runs produce 481,280 stereo frames at 48 kHz, and the 401-field actions run produces 321,120, consistent with the nominal rate to the SPU2's 32-sample output batching. Fresh-boot timing also retains its expected initial silent period. These captures check the mixer, not physical audio-device latency.

Native and web builds succeed. A headless Chrome WebGPU smoke test loaded a version 3 gameplay state and advanced from frame 17000 to 18011 without exceptions. It ended at a displayed 60 fps with 79 ms queued audio, after startup and a transient dip; this is a smoke test, not a long-run pacing measurement. All nine GPU game regressions pass byte-for-byte against the refreshed goldens. The complete software suite also passed before the final interrupt-latch persistence adjustment. The final targeted software recheck passes all eight selected cases (maximum mean 0.12, p99 1); it omits the long shop-cutscene case, which is covered by the full GPU run and pending-edge state tests.

The changed field/GS timing intentionally changes which field and animation sample appears at a numbered screenshot. All eleven old/new captures across the nine regression cases were visually reviewed; scenes and player positions remained intact. The previous golden set is retained locally as `games/dmc/golden-before-video-timing`, and the current goldens were explicitly refreshed for this timing change. These game-output files remain uncommitted. Updating our own goldens establishes determinism, not agreement with a hardware capture.

```powershell
cmake -S . -B build/native-dmc -DPS2_BUILD_ACCURACY_TESTS=ON
ninja -C build/native-dmc ps2run ps2_accuracy ps2_spu2_accuracy
ctest --test-dir build/native-dmc --output-on-failure
$env:PS2_GS = 'gpu'
python scripts/dmc_regress.py --out games/dmc/regress-video-timing-verify-gpu
# For a separate software run:
$env:PS2_GS = 'soft'
python scripts/dmc_regress.py --out games/dmc/regress-video-timing-verify-soft
```
