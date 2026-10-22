# Audio accuracy - 2026-10-22

## Implemented: SPU2 Gaussian interpolation

ADPCM voices now use a four-tap integer filter in place of linear interpolation. The literal 512-entry coefficient table comes from [nocash's published SPU hardware documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/#4-point-gaussian-interpolation). Every one of its 256 phase rows was checked against [PCSX2's SPU2 table](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/interpolate_table.h). No mathematical approximation or table generator is used at runtime.

Pitch-counter bits 4-11 select the phase. Each signed product is shifted separately by 15 before summing, following the documented integer behavior and [PCSX2 SPU2 mixer](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp). Three samples of history follow each voice across ADPCM blocks and loop boundaries and are cleared on key-on. PCM AutoDMA input bypasses this filter.

The table's SHA-256 as 512 signed little-endian 16-bit words is `3e221a5b25de34ca9ad1ea945ad90fbe8171c5e3f7c52af15b232f9c36fbd40d`. The disc-free test pins an independent FNV checksum and numeric output vectors.

## Save states

Gaussian interpolation introduced state version 4 with interpolation history. The current format is version 5 for video-event changes; it retains that history and loads versions 2-4. The serialized voice layout is unchanged. Versions 2/3 lack history, so it starts at zero; a voice near a block boundary can have a short filter transition until history is rebuilt. Resumes from version 4 onward preserve the filtered output exactly. Older runtime binaries cannot load newer-format states.

## Implemented: per-voice dry/wet routing

VMIXL/VMIXR and VMIXEL/VMIXER now independently gate each of the 24 voices into the left/right dry and wet voice buses on both cores. Muted voices still decode samples, advance ADSR/volume sweeps and write raw voice capture data. Each voice bus saturates to signed 16-bit before master gain, and all four capture rings contain the routed pre-MMIX voice sums. A wet capture is the reverb input voice bus, not a computed reverb return.

Register addresses and routing order were checked against [PCSX2's SPU2 register map](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/regs.h), [mixer](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp), and [initialization/register writes](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/spu2sys.cpp). Reference initialization enables all 24 bits of each voice mask. Existing saved states already store these registers; no layout or version change is needed and no saved masks are overwritten on load.

The voice-routing stage brought the SPU2 target to 35,431 assertions. Coverage includes all 16 dry/wet gate combinations, both cores, voices 0/15/16/23, output and capture samples, muted-state advancement, exact state restoration, wet capture IRQA, independent bus saturation and raw voice capture. The dry core-routing stage below extends this coverage.

## Validation

The routing recordings under `games/dmc/audio-accuracy/voice-routing` contain 1,520,704 (boot), 481,280 (shop), 321,120 (actions), and 481,280 (hall) stereo frames at 48 kHz. All four are byte-identical to the preceding video-timing captures, with no clipped samples or detected dropouts. Nine GPU game cases pass without refreshing frames. Native/web builds succeed. A 20-second headless browser run reached frame 17772, ending near 60 fps with 73 ms of queued audio; warmup repeatedly emptied the queue before it stabilized.

## Browser startup and starvation recovery

The user reported scratchy audio at the beginning. The worklet consumed any arriving samples immediately, decayed its last sample when empty, and resumed new PCM without a transition. This can turn startup starvation into repeated discontinuities. Playback now waits for 2,880 stereo frames (60 ms at 48 kHz) at startup and after an underrun. It fades to zero over 240 frames when starved and fades in over 240 frames when resuming; steady playback retains the original PCM values. The producer's existing 80 ms pacing target is unchanged.

`node scripts/test_web_audio.mjs` executes the actual embedded worklet with a deterministic shared-memory producer. It verifies silent prebuffering without consuming samples, exact steady PCM, partial-quantum starvation, bounded fade steps, recovery, stereo, pacing activation, and 32-bit counter wrap. Device startup buffering does not affect the emulated mixer WAV captures. Physical listening and sustained overload still need separate coverage.

## Implemented: dry core output routing

MMIX now gates dry voice and AutoDMA input channels independently. Core 0 feeds core 1's external path through core 0 master gain, the signed core 1 external gains, MMIX external gates and core 1 master gain. Core 1 supplies the final output; the cores are no longer added independently at the host. Dry sources saturate before master gain, and the core 0 capture rings contain the pre-external-gain signal. This follows the [PCSX2 core mixer](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp) and [MMIX register mapping](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/spu2sys.cpp). Existing state fields already contain MMIX and all gains; the state format is unchanged.

The SPU2 target now has 37,479 assertions, including independent voice/input/external channel gates and zero, positive and negative external gains. Seven CTest targets and all nine GPU game regressions pass without a reference-frame refresh. Native and browser builds succeed. Four new captures in `games/dmc/audio-accuracy/core-routing` retain the same sample counts and have no clipped samples or detected dropouts. Applying the additional core gain stages changes at most three signed 16-bit levels in these clips; RMS changes by less than 0.0015 dB. These are reference-model checks, not physical PS2 capture comparisons.

The Gaussian interpolation stage initially ran 10,457 disc-free assertions: coefficient identity, exact phase vectors, ignored low fractional bits, per-product rounding, both cores, stationary/half/unity/high pitches, block and loop boundaries, stereo gain, key-on history reset, state restoration, deterministic legacy-state migration, PCM bypass, and 48 kHz sample counts. These remain part of the current 37,479-check SPU2 suite.

```powershell
cmake -S . -B build/native-dmc -DPS2_BUILD_ACCURACY_TESTS=ON
ninja -C build/native-dmc ps2run ps2_spu2_accuracy
ctest --test-dir build/native-dmc --output-on-failure
python scripts/dmc_regress.py --out games/dmc/regress-audio-gaussian
```

Four recordings were captured with the previous mixer and the new mixer using identical frame limits and scripted input. They remain local under `games/dmc/audio-accuracy/{linear,gaussian}`:

| Capture | Stereo frames at 48 kHz | RMS level change | Energy above 16 kHz, relative to total |
|---|---:|---:|---:|
| Fresh boot through title/movie | 1,519,168 | -0.008 dB | -40.33 to -43.55 dB |
| Shop cutscene, 9000 to 9601 | 480,800 | -0.098 dB | -31.69 to -36.68 dB |
| Gameplay jump/gun/sword, 17000 to 17401 | 320,800 | -0.039 dB | -45.84 to -58.79 dB |
| Castle hall, 21900 to 22501 | 480,800 | -0.037 dB | -61.59 to -81.24 dB |

All four have identical sample counts before/after and no clipped samples or detected dropouts according to `scripts/audio_check.py`. The movie portion of the boot capture from 20 seconds onward is byte-identical, confirming the streamed PCM path was preserved. Reduced high-frequency energy is consistent with the changed filter; it does not substitute for a hardware capture comparison. Nine game regressions pass without refreshing golden frames, and the four CTest targets pass.

Those before/after counts isolate the interpolation change and precede the video timing update in [VIDEO_TIMING.md](VIDEO_TIMING.md). The later change from exact 60 to nominal 59.94 fields/s intentionally generates slightly more 48 kHz samples for a fixed number of NTSC fields; it does not change the audio sample rate or voice pitch.

Native and web builds succeed. A 20-second headless Chrome check loaded the existing version 3 gameplay state and advanced from frame 17000 to 17954 without console exceptions. It ended at 60 fps with 58 ms of queued audio, after startup warmup and transient frame-rate dips. This checks browser execution and buffering, not the physical audio device.

## Remaining work

- Reverb processing/return is still unimplemented; routed wet voice capture rings now contain their input samples.
- Voice noise and pitch modulation still need implementation and focused tests.
- MMIX wet input/external routing awaits the reverb processor; dry routing, external gain and the core-to-core output chain are implemented. Core mute/SPDIF special modes and master-volume sweep behavior remain incomplete.
- ADPCM prefetch/end/loop interrupt timing and detailed ADSR/sweep timing need hardware-derived tests. This change preserves the existing scheduling and envelope model.
- Native/browser audio-device underruns and long-session synchronization need longer playback coverage. WAV captures exercise the emulated mixer rather than the output device.
