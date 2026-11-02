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
- **Each buffer address is computed once per pixel.** Pixel access is split into `pixel::locate` (page/block/column math) and `pixel::load`/`store` (format semantics); `read`/`write` are their composition. `plot` locates the depth spot once for test and write, and the frame spot once (lazily, after the depth test) for destination alpha, blend read and masked/16-bit write. A random sequence of 4M `read_pixel`/`write_pixel` calls over all 64 PSM values gives the same hash on the old and new builds.
- **Per-row spans for triangles.** Rows used to test every pixel of the bounding box. Vertices are on a 1/16 grid under 4096, so each edge function is exact in double, and the float edge expression differs from the exact value by at most `(|AB| + |CD|) * 2^-23`. Each row now scans only the exact span widened by twice that bound plus one pixel; the original float test and weights still decide every candidate. Triangles with larger coordinates fall back to the full box.

Measured with a 6-scene, 18,000-primitive workload (small, mipmapped and large triangles, sprites, lines, points over all frame/texture/depth formats, blending, fog, dithering, alpha/depth/destination tests):

| | Baseline (`a6bcfce`) | Now |
|---|---|---|
| Instructions (callgrind) | 11.07 G | 9.23 G (-16.7%) |
| Wall time, best of 5, x86-64 | ~820–845 ms | ~720 ms (-12–14%) |
| GS memory checksum | `74885681BD3A555B` | `74885681BD3A555B` |

A separate differential harness drew 144 random scenes, including full-range 16-bit coordinates, slivers, degenerate triangles and vertices on pixel centers (edge ties), through a frozen copy of the old rasterizer and the new one: GS memory was byte-identical in every scene. The harness is not committed (it needs a copy of the old source); `ps2_accuracy --raster-bench` is the committed equivalent of the checksum check.

### IPU (`runtime/src/ipu/ipu.cpp`)

- **Sparse IDCT.** Most MPEG coefficients are zero. A sum starting at +0 is never -0, so skipping zero products and all-zero rows, while each output keeps adding its remaining terms in the original order, gives the same doubles. A `floor` via conversion replaces the libm call for the final rounding.
- **Bit reader fast path**: `bits_at` copies eight bytes at once when they are in the FIFO buffer instead of bounds-checking each byte.

IDCT micro-benchmark (200,000 blocks, x86-64, best of 5, two runs, all outputs identical to the old transform): DC-only blocks ~2.5x faster, 3–8 coefficients ~1.7–1.8x, 20 coefficients ~1.15x; fully dense blocks (all 64 coefficients, rare in practice) ~5–8% slower. Not measured in wasm or on real movie data.

### Debug watch hooks (`runtime/include/ps2/memory.hpp`)

`watch_span` (the `PS2_WATCH` reporter for DMA, SIF and file-read writes) was an out-of-line call that only tested a flag, made per qword on fromSPR DMA. The flag check is now inline; the reporting body is `watch_span_hit`.

## How to check

```sh
cmake -S . -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2_BUILD_ACCURACY_TESTS=ON
ninja -C build/tests && ctest --test-dir build/tests
PS2_GS=soft build/tests/ps2_accuracy --raster-bench 5   # best time + GS memory checksum
```

A rasterizer change that should not alter output must print the checksum of the build it replaces (`74885681BD3A555B` here). The time is noisy on shared machines; compare best-of-N or instruction counts.

## Tried and dropped

- **Specializing `plot`/`triangle` on texturing** (template on `tme`): 0.3% more instructions. The remaining per-pixel cost is spread over many small pieces (no line above ~3%), so branch removal doesn't pay, and every specialization doubles code size in the wasm build.
- **Computing the frame address before the depth test**: more work, since many pixels fail Z. It is computed after the test unless destination alpha needs it first.
- **Gather-based sparse IDCT**: slower than the dense loop it replaced, because the dense loop vectorizes. The committed version keeps vectorizable inner loops.

## Not changed, and why

- **Triangle setup / `make_setup` per primitive.** Small compared with pixel work; caching it would need a reliable invalidation key, and tests drive `raster::draw` with local `State` objects that bypass `g_setup_generation`.
- **Span interpolation / incremental attributes.** Would change float rounding of colors, UVs and depth. Too risky without the game regressions.
- **VIF `pending` front-erase.** `process()` erases the consumed prefix (O(n) per resume). Only matters with frequent stalls on large chains; fixing it touches the save-state layout. Not measured as hot.
- **VU0 macro ops (`vu.hpp`).** The generated EE code passes register numbers and lane masks as literals, so `lane()` checks, `flags()` lanes and register indexing already constant-fold. What remains is the bit-exact `fp::add`/`fp::mul` emulation (PS2 truncates, host floats round), which can't be swapped for host arithmetic. Little left without changing semantics.
- **VIF1 shadow posting / GS queue.** The cost is the payload copy into the VU ring, needed because the EE can overwrite the source buffer once the DMA completes. The ring's atomics only bounce cache lines when a side goes idle.
- **SPU2 mixing.** Already batched per 8 samples with per-ring IRQ range checks; nothing obvious.
- **GIF parsing, `dispatch()`, guest memory access.** Already fast paths (table lookups, one range check, inline fast loads/stores).

## Largest remaining opportunity: guest registers in the recompiled EE code

`eerecomp` emits every guest register access as `c.r[N]` (a field of the `Cpu` struct passed by reference), and every guest store writes `g_ram` through `memcpy`. The compiler must assume such a store can alias `c`, so it reloads registers from memory after each store and after each out-of-line call (the slow memory path, `syscall`, `vu0_*`). Checked with clang -O3 on a block shaped like the generated code (`sw t0,0(a0); sw t1,4(a0); addiu a0,a0,8`): `a0` is reloaded from `c` after each store, and the `PS2_WATCH` flag is re-read before every store for the same reason. Recompiled EE code is the largest consumer on the emulator thread, so caching registers in locals within a block (written back before calls, stores to the cached registers' backing and exits, the way `vurecomp` already keeps VF registers in locals) is likely the biggest single CPU win left. It touches all 65 MB of generated code and has to be validated with the game regressions and probe diffs, so it was not attempted here.

## Next steps

1. Run `python scripts/dmc_regress.py` (GPU and `PS2_GS=soft`) and rebuild the web page to confirm on real frames; expect identical screenshots.
2. Measure the browser without WebGPU (`webprobe.mjs --nogpu`) at the castle door state to see how much of the ~10 fps changes.
3. Recompiler register caching (above), validated with `dmc_regress.py` and `scripts/probediff.py`.
4. For larger software-renderer gains: split rows across worker threads (each pixel keeps its primitive order; render-to-texture and transfers need barriers). The web build would need a larger `PTHREAD_POOL_SIZE`, and it needs the game regressions and a browser check.
