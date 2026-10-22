# Frame and rendering accuracy audit - 2026-10-21

This audit covers video-event scheduling, GS destination-alpha testing, and the native screenshot regression checks. Matching the software renderer and GPU or reproducing our own golden frames establishes consistency; it does not establish a match to PS2 hardware.

## Fixed

- **GPU surface speckles at enlarged resolution.** The earlier software speckle fix is recorded in `ENGINEERING_LOG.md` under "Software renderer speckles (fixed)": constant alpha could become 127.9999 and fail GEQUAL 128, and repeated triangles needed a stable vertex order. The GPU path still used a weighted sum of three absolute colors and the original vertex ordering. It now interpolates color/STQ from an anchor plus differences, preserving constant channels exactly, and sorts triangle vertices while retaining the original flat-shading provoking vertex. The user's torch-corridor view was reproduced from the local exploration checkpoint and checked at 3x in the browser; its regular bright/dark surface dots disappeared. Disc-free tests check constant-alpha and reordered GEQUAL depth passes, including every supersampled layer.
- **Mip levels and minification filtering.** The bridge trace requests explicit mip levels (`TEX1=FFFFFF3D00000124`, with positive calculated LOD on many vertices), but both renderers previously always sampled level zero with MMAG. Both now use signed K, LCM/FST, L and interpolated Q to choose MMAG/MMIN and explicit MIPTBP1/2 levels, including nearest-level and trilinear selection through level six. Dimensions, coordinates and region bounds scale with the selected level. GPU batch hazards include mip ranges; level-zero decoded-cache entries are bypassed for other levels. Reference: [PCSX2's LOD/filter model](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/Common/GSVertexTrace.cpp) and [software mip selection](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/SW/GSRendererSW.cpp). Host log2 and floating-point filter weights remain approximations; automatic MTBA chain generation is not implemented.

- **Vblank clock drift.** The runtime scheduled each vblank relative to the time its callback actually ran. A translated EE block that overshot the event deadline shifted every later field by that overshoot. Vblank callbacks now retain their scheduled deadline and advance from that deadline. Restored save-state callbacks use the stored event deadline too. The disc-free test finishes EE work 137 cycles late on eight consecutive fields and verifies the next deadline and field count in both PAL and NTSC, including a save/load round trip.
- **Destination-alpha testing on PSMCT24.** Both renderers treated the synthesized destination alpha as a real alpha bit, rejecting all fragments when DATE=1 and DATM=0. DATE is now ignored for 24-bit framebuffers. The synthetic tests cover both DATM values and both stored high-bit values for PSMCT32, PSMCT24, PSMCT16 and PSMCT16S, check depth rejection as well as color, and check preservation of the unused 24-bit framebuffer byte. Reference: [PCSX2 software renderer's destination-alpha test](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/SW/GSDrawScanline.cpp), which applies DATE to 32/16-bit formats. The implementation here is a format guard in the existing runtime.
- **Screenshot error measurement.** `dmc_regress.py` claimed to measure channel error but converted the RGB differences to luminance first. Blue-only errors were strongly discounted. Mean and p99 now measure all RGB channel samples equally. Nonzero runner exits and missing required states fail the run; unknown `--only` case names are rejected.
- **16-bit GS dithering.** Both renderers now apply the signed 3-bit DIMX matrix when DTHE is enabled on PSMCT16/PSMCT16S. RGB offsets are added after blending and before final COLCLAMP clamp/wrap and framebuffer packing. Alpha is unaffected. GPU setup records carry the full matrix, and matrix indexing uses native GS coordinates at every internal scale. This stage order follows the [PCSX2 software renderer's WriteFrame path](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/SW/GSDrawScanline.cpp).

## Validation

- The new mip tests cover 120 level/filter/LOD combinations and six spatial ramp samples; constant-alpha/coplanar-pass tests cover all GPU scale layers. The complete GS target has 6,307 software checks and 6,308 GPU checks. Seven CTest targets pass; GPU checks pass at 1x and 3x (the initial mip/ramp checks also passed at 2x). Native and web builds succeed. Eleven before/after reference frames were reviewed; goldens were intentionally refreshed for mip filtering/interpolation, with the prior set retained locally in `games/dmc/golden-before-mipmapping`. The five changed software captures compare to the new GPU goldens with maximum mean 0.091 and p99 1. Full game checks and their final results are recorded in `ENGINEERING_LOG.md`.

- Native runner rebuilt; nine GPU game regressions pass against reviewed golden frames refreshed for the video timing change. The preceding GS fixes had preserved the earlier goldens; the prior set remains local as `games/dmc/golden-before-video-timing`.
- All nine software game regressions also pass with the stricter RGB metric: maximum mean error 0.13 and p99 error 1 against the GPU goldens.
- Web build succeeds. Linking required TEMP/TMP/TMPDIR inside the workspace after a sandbox temporary-directory permission error. The final timing smoke test in headless Chrome loaded a legacy state and advanced from frame 17000 to 18011 without exceptions. The localhost debugging connection required running the browser test outside the sandbox. See [VIDEO_TIMING.md](VIDEO_TIMING.md) for current validation scope.
- Disc-free GS checks: 6,180 assertions each on software and GPU; GPU checked at 1x, 2x and 3x. Dithering tests cover the 4x4 matrix repeated across an 8x8 region, signed and reserved bits, both clamp modes, additive/subtractive blends, framebuffer masks, alpha preservation, DTHE off, and unaffected 32/24-bit formats. Four checks drive actual GS register writes to change DIMX/DTHE within one GPU batch, testing setup-cache invalidation and record stride.
- Disc-free timing checks cover five mode profiles, 100,000-field sums, blank/GS edges, rational scanlines, counter IRQs, mode changes and state migration. See [VIDEO_TIMING.md](VIDEO_TIMING.md).
- Eight Python tests cover identical images, individual channels, blue-only errors, mismatched dimensions, nonzero runner exits, missing states, unknown case names and separate output directories. Use `--out` to give concurrent game-regression runs independent output directories.

Build and run the synthetic checks without game data:

```powershell
cmake -S . -B build/native-dmc -DPS2_BUILD_ACCURACY_TESTS=ON
ninja -C build/native-dmc ps2_accuracy
ctest --test-dir build/native-dmc --output-on-failure
$env:PS2_GS = 'gpu'
$env:PS2_SCALE = '1' # Also checked with 2 and 3.
& build/native-dmc/ps2_accuracy.exe --gpu
python scripts/test_dmc_regress.py
```

## Remaining accuracy work

1. **Custom video modes.** Standard NTSC/PAL interlaced and double-strike timing plus 480p now use shared, reference-based profiles with rational field/scanline scheduling, mode-dependent blanking and delayed GS FIELD updates; see [VIDEO_TIMING.md](VIDEO_TIMING.md). HDTV/VESA and arbitrary raw timing-register programming still need dedicated models. Golden frames were reviewed and refreshed for the intended timing change, with the previous set retained locally.
2. **Raster precision.** Color, texture and fog interpolation still use host floats; the software color epsilon is an approximation. Bilinear sampling also uses floating-point weights. GPU/software agreement cannot prove GS fixed-point behavior. Add hardware-derived test vectors before replacing these rules.
3. **External reference frames.** The stabbing-cutscene fire patches remain unconfirmed. No new hardware or PCSX2 reference captures were available in this audit. Combat coverage also remains incomplete.
