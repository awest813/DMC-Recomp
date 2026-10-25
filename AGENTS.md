# Agent Instructions - PS2 Web Recomp (Devil May Cry)

For coding agents working in this repository. Read `HANDOFF.md` first (status, build, test, open issues); `ENGINEERING_LOG.md` has the history behind each fix.

## Mission

Port Devil May Cry (`SLUS_202.16`) to the browser through this static-recompilation runtime, fixing the generic runtime rather than adding game-specific code. It currently plays natively and on the web from boot into Mission 1; see `HANDOFF.md` > Open issues for what is next.

## Ground rules

- Never hand-edit `generated/dmc`. Fix the recompiler or runtime and regenerate.
- Never commit or publish game data: the ISO, generated C++, built binaries, save states, screenshots, golden frames, audio dumps, or anything under `games/`. `.gitignore` covers these; check `git status` before committing.
- User-authorized exception (2026-10-22): publish the four `ps2web.html/js/wasm/data` player artifacts as versioned GitHub release assets and bundle them in Pages so visitors select only an ISO. Keep these artifacts out of source commits. This exception does not include ISOs, generated C++, states or other game artifacts.
- Commit only when the user asks. Use a feature branch from `main` for new work unless the user explicitly requests otherwise. `main` contains the integrated DMC port and deploys GitHub Pages.
- Third-party binaries (`third_party/wgpu-native`, `third_party/SDL2-*`, `tools/emsdk`) stay uncommitted. Ask the user before downloading anything new.
- Keep the existing code style. Comment only non-obvious hardware or runtime behavior.
- Run `python scripts/dmc_regress.py` after runtime changes; refresh golden frames (`--record`) only for intended output changes, and say so.
- Validate SIF/CD/kernel fixes from a fresh boot as well as from save states; old states can hold stale IOP requests.

## Environment

- Windows 11, PowerShell 5.1 and Git Bash. `clang++` 22.1.8 (`x86_64-w64-windows-gnu`), CMake 3.31.12, Ninja 1.13.2, Python 3.14 (Pillow, numpy), Node 26.
- GPU: Intel Arc A750. WSL is not usable for builds and is not needed.
- ISO: `..\Devil May Cry.iso` from the repository root. Boot ELF: `C:\Users\allen\AppData\Local\Temp\dmc-audit\SLUS_202.16` (also copied to `games/dmc/disc/`).
- Windows Defender has quarantined `tools/emsdk/upstream/bin/wasm-opt.exe` once (a false positive). Do not change security settings; ask the user to restore it.
- Shell gotchas: Git Bash heredocs feeding Python can turn `\n` inside string literals into real newlines; write such scripts to a file. PowerShell variables are case-insensitive (`$game` and `$Game` are the same variable). Windows filenames cannot contain `:`.

## Runner basics

`build/native-dmc/ps2run.exe <elf> --disc <iso> --memcard <card.ps2> [options]`

- `--frames N` halts at frame N. `--press frame:button[+button][:frames]` scripts input; buttons: `cross circle square triangle start select up down left right l1 r1 l2 r2 l3 r3` and stick directions `lup ldown lleft lright`.
- `--frames-dir dir --frame-every n` dumps PPM frames (the directory must exist). `--misses file` writes unresolved guest addresses; check it after exploratory runs.
- `--save-state N:file` saves at frame N; it is checked after the frame-limit halt, so use `--frames N+1`. `--load-state file` resumes; frame numbers stay absolute.
- `--window` / `--fullscreen` (SDL build), `--widescreen`, `--trace-syscalls`, `--trace-threads`, `--trace-iop` (very large), `--trace-iop-hle`.
- Boot inputs that reach the title and start a new game: `--press 300:start --press 600:cross --press 660:cross --press 720:cross --press 780:cross --press 840:cross --press 900:cross` (`scripts/dmc_run.sh <frames> <log>` adds Start/Cross presses after that).

## Debugging reference

Environment variables (native unless noted):

| Variable | Effect |
|---|---|
| `PS2_GS=gpu\|both\|soft` | Renderer: GPU (default), GPU plus software mirror, or software only. |
| `PS2_GS_COMPARE=1\|show` | With `both`: report GPU vs software pixel differences per dumped frame; `show` dumps the GPU image. |
| `PS2_SCALE=1\|2\|3` | Internal resolution. |
| `PS2_ONE_THREAD=1`, `PS2_NO_VU_THREAD=1` | Window build: disable the threaded pipeline or just the VU thread. |
| `PS2_FRAMES_ONLY=f1,f2` | Limit `--frames-dir` dumps to those frames. |
| `PS2_DUMP=file` | EE RAM at halt (36 MiB). |
| `PS2_DUMP_VRAM=bp,bw,psm,w,h,file;...` | GS memory regions at halt. |
| `PS2_DUMP_AUDIO=file.wav` | SPU2 output for the run. |
| `PS2_PEEK_FRAME=addr,n` | Every vblank: n words at addr, printed as floats (`[peek]`). |
| `PS2_PEEK_WORDS=a,b,...` | Every vblank: those words in hex (`[peekw]`). |
| `PS2_WATCH=base,len` | Print every write into that RAM range with PC/RA. |
| `PS2_TRACE_DISPLAY=1` | Display registers and sample pixels per dumped frame. |
| `PS2_TRACE_PRIM=N\|fN` | Primitive setup from primitive N, or for all of frame N. Filters: `PS2_TRACE_PRIM_BP=<hex block>` (frame or texture base), `PS2_TRACE_PRIM_TYPE=<0-6>`. Software renderer. |
| `PS2_TRACE_TEXSUM=N` | Unique texture/target setups drawn in frame N with counts and screen boxes (software renderer). |
| `PS2_TRACE_PIXEL=x,y` | Every fragment at one pixel: texture, uv, color, blend, destination, depth/alpha tests (software renderer; GS coordinates, so halve Y for 448-line interlaced output). |
| `PS2_SKIP_TBP=<hex>` | Skip textured draws from that texture base (software renderer). |
| `PS2_TRACE_INTC=1`, `PS2_TRACE_DMA=1`, `PS2_TRACE_MC=1`, `PS2_TRACE_SPU2=1`, `PS2_TRACE_IPU=1` | Interrupt handlers, DMA, memory card, SPU2, IPU logging. |
| `PS2_SPIKES=ms`, `PS2_TIME=from,count` | Frames slower than ms; average frame time over a range. |
| `PS2_VU_RECORD=dir` | Record VU1 microprograms for `vurecomp`. |
| `PS2_VU_CAPTURE=file:skip:count`, `PS2_THREAD_PROFILE=file` | VU1 call capture for `recomp/vubench.cpp`; native thread sampler for `scripts/thread_profile.py`. |
| `PS2_PROBE_COUNT=from,to`, `PS2_PROBE_ONLY=<hex pc>` | Probe build only: count hits per probe in a frame window, or print one probe's registers. |
| `PS2_KEYS=controls.txt`, `PS2_QUICKSAVE=file`, `PS2_QUICKSAVE_AT=frame` | Window build: key layout, F5 target, and a test trigger for F5. |

Probe build: list addresses (e.g. every `T`/`t` symbol from `llvm-nm`), run `eerecomp ... --probes list --out generated/dmc-probe`, build it into its own directory, then use `PS2_PROBE_COUNT` and `python scripts/probediff.py a.log b.log` (symbols from `llvm-nm -n <elf> > games/dmc/syms.txt`) to diff which game functions run.

Web page URL parameters: `state=<file in build/web-dmc>`, `press=frame:button,...`, `keys=input:Key,Key;...`, `PS2_SCALE=1..3`, `aspect=16:9`, `tracedisc` (log slow disc chunks), `nocard`, `gpu=low`.

## DMC memory map (NTSC-U)

| Address | Meaning |
|---|---|
| `0x5B71B0` | Player work struct; position at `+0x110` (x, y, z floats), so `PS2_PEEK_FRAME=5B72C0,3`. Gravity is -Y. |
| `0x754130` | Game flag word; `0x80000` set while enemies are active (shows the HUD). |
| `0x9EE360` | `Gauge` (HUD) struct; bytes `+141/+142` show the vitality gauge. |
| `0x5BEE18` | Vsync counter incremented by `Main_Get_Vsync`; `main` waits on it every frame (normal). |

The ELF has symbols: `llvm-nm -n SLUS_202.16`. The 14 `t_*` overlay sections at `0x1F00000` are size 0; nothing to capture.

## Known false leads

- `main` spinning at `0x15B4D8` is its normal per-frame vsync wait.
- `ioman` #20/#21 are registered stubs returning -1; `cdvdman` #51 is absent but unused on the tested path.
- The `secrman` version mismatch does not affect HLE lookup (keyed by library name and index).
- `[iop-tty] SPU:T/O` and SIF "waiting for reply" messages at boot are harmless.
- The bridge door in the prologue and the red door in the castle hall do not open: the route goes down the cliff path, and the red door is sealed by the story.
- The HUD gauge is hidden while exploring by design (`Life_gauge_move`).
