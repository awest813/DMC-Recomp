# Performance Audit

2026-11-02. Scope: the runtime's hot paths, reviewed without the disc. Game regressions (`scripts/dmc_regress.py`), the web build and the GPU renderer were **not** run for this pass; every change was instead proven output-identical with disc-free differential tests.

## Where time goes (from earlier profiles)

| Thread | Main costs | Source |
|---|---|---|
| VU (web) | AOT VU1 programs, VIF unpack (now ~3%) | ENGINEERING_LOG 2026-10-20 |
| Emulator (web) | IPU `decode_macroblock` 9–14% during movies, VIF1 shadow posting ~8%, scalar VU0 macro ops ~8% | same |
| GS (web, no WebGPU) | Software rasterizer, 0% idle at ~10 fps | ENGINEERING_LOG 2026-10-21 |
| Native window | EE/VU code, IPU decode, GPU submission; mostly waiting otherwise | ENGINEERING_LOG 2026-10-22 |

## Changes

All of these keep results bit-identical.

### Software rasterizer (`runtime/src/gs/raster.cpp`, new `runtime/src/gs/pixel.hpp`)

- **Swizzled pixel access is inline.** `pixel::read`/`pixel::write` (and the page math with constant page sizes) live in a header, so the rasterizer no longer makes a cross-file call per framebuffer, depth and texel access. `gs::read_pixel`/`write_pixel` remain the public entry points and forward to it.
- **Texture sampling dispatches on the format once per sample**, not per texel (bilinear reads four texels; trilinear eight). Mip levels copy a small `Tex` struct instead of the whole `Setup` per pixel.
- **`floor` without libm** in texel coordinates on baseline x86-64 (`floor_int`, with an INT_MIN guard matching the converted `std::floor`).
- **Per-row spans for triangles.** Rows used to test every pixel of the bounding box. Vertices are on a 1/16 grid under 4096, so each edge function is exact in double, and the float edge expression differs from the exact value by at most `(|AB| + |CD|) * 2^-23`. Each row now scans only the exact span widened by twice that bound plus one pixel; the original float test and weights still decide every candidate. Triangles with larger coordinates fall back to the full box.

Measured with a 6-scene, 18,000-primitive workload (small, mipmapped and large triangles, sprites, lines, points over all frame/texture/depth formats, blending, fog, dithering, alpha/depth/destination tests):

| | Baseline (`a6bcfce`) | Now |
|---|---|---|
| Instructions (callgrind) | 11.07 G | 9.34 G (-15.6%) |
| Wall time, best of 5, x86-64 | ~845 ms | ~730–760 ms (-11–14%) |
| GS memory checksum | `74885681BD3A555B` | `74885681BD3A555B` |

A separate differential harness drew 144 random scenes, including full-range 16-bit coordinates, slivers, degenerate triangles and vertices on pixel centers (edge ties), through a frozen copy of the old rasterizer and the new one: GS memory was byte-identical in every scene. The harness is not committed (it needs a copy of the old source); `ps2_accuracy --raster-bench` is the committed equivalent of the checksum check.

### IPU (`runtime/src/ipu/ipu.cpp`)

- **Sparse IDCT.** Most MPEG coefficients are zero. A sum starting at +0 is never -0, so skipping zero products and all-zero rows, while each output keeps adding its remaining terms in the original order, gives the same doubles. A `floor` via conversion replaces the libm call for the final rounding.
- **Bit reader fast path**: `bits_at` copies eight bytes at once when they are in the FIFO buffer instead of bounds-checking each byte.

IDCT micro-benchmark (200,000 blocks, x86-64, best of 5, two runs, all outputs identical to the old transform): DC-only blocks ~2.5x faster, 3–8 coefficients ~1.7–1.8x, 20 coefficients ~1.15x; fully dense blocks (all 64 coefficients, rare in practice) ~5–8% slower. Not measured in wasm or on real movie data.

## How to check

```sh
cmake -S . -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2_BUILD_ACCURACY_TESTS=ON
ninja -C build/tests && ctest --test-dir build/tests
PS2_GS=soft build/tests/ps2_accuracy --raster-bench 5   # best time + GS memory checksum
```

A rasterizer change that should not alter output must print the checksum of the build it replaces (`74885681BD3A555B` here). The time is noisy on shared machines; compare best-of-N or instruction counts.

## Not changed, and why

- **Triangle setup / `make_setup` per primitive.** Small compared with pixel work; caching it would need a reliable invalidation key, and tests drive `raster::draw` with local `State` objects that bypass `g_setup_generation`.
- **Span interpolation / incremental attributes.** Would change float rounding of colors, UVs and depth. Too risky without the game regressions.
- **VIF `pending` front-erase.** `process()` erases the consumed prefix (O(n) per resume). Only matters with frequent stalls on large chains; fixing it touches the save-state layout. Not measured as hot.
- **VU0 macro ops (`vu.hpp`).** Scalar `fp::add`/`fp::mul` emulate PS2 rounding exactly; a SIMD rewrite needs the game's VU0 code to validate.
- **SPU2 mixing.** Already batched per 8 samples with per-ring IRQ range checks; nothing obvious.
- **GIF parsing, `dispatch()`, guest memory access.** Already fast paths (table lookups, one range check, inline fast loads/stores).

## Next steps

1. Run `python scripts/dmc_regress.py` (GPU and `PS2_GS=soft`) and rebuild the web page to confirm on real frames; expect identical screenshots.
2. Measure the browser without WebGPU (`webprobe.mjs --nogpu`) at the castle door state to see how much of the ~10 fps changes.
3. For larger software-renderer gains: specialize `plot` on common state (no texture, no blend, 32-bit frame), or split rows across worker threads; both need the game regressions to validate.
