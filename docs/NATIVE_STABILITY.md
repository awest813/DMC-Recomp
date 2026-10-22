# Native session stability

The native host uses the shared video-mode field period, with continuous wall-clock deadlines. A low-pass audio queue measurement gently corrects device-clock drift (at most 1 ms per field). The target is approximately 80 ms of queued sound after pacing; device block boundaries no longer release several fields at once. Long stalls and video-mode changes rebase the deadline. No emulated video rate or blanking interval is changed.

Sound starts after a 60 ms prebuffer and fades in over 5 ms. Starvation re-enters prebuffering. A stopped, removed or non-draining audio device releases pacing to the video clock instead of trapping the emulation thread. The non-draining watchdog is 250 ms, with queue progress resetting it. Excess stale audio is bounded rather than allowed to grow indefinitely.

Keyboard and gamepad inputs are neutral while the native window lacks focus. Controllers remain connected, and rumble is stopped. Sticks use a rescaled deadzone, preserving their full range. Opposing keyboard stick directions cancel. The threaded render loop polls SDL at most every 4 ms, avoiding event/controller polling for every small render submission.

Host input is published as one synchronized controller state, so a guest packet cannot mix buttons/axes from different host updates. Snapshot restoration republishes the current native input immediately; held buttons from the checkpoint do not remain stuck behind an unchanged host cache. Each poll refreshes input even when unchanged, covering guest controller resets too. Controller serialization, host writes and guest reads share a lock. Host-visible frame counters and the shutdown flag are atomic.

Keyboard files accept surrounding whitespace. An invalid key name leaves that input's previous binding intact and reports the problem in the game log, instead of silently unbinding a control. An explicitly empty binding still unbinds the input.

F5 requests a quick save at a safe emulation boundary. The title reports success or failure; use **Resume quick save** in the launcher to load it. There is no in-session quick-load shortcut. Snapshots are version 5 and older version 2–4 loads remain supported. Loading an invalid checkpoint through the runner refuses startup. Quick saves do not include the memory-card contents; card files remain independent.

Checkpoints and memory cards are staged beside their destinations, closed and checked before replacement. Failed replacements preserve the previous destination and clean up staging files. Failed card writes keep their dirty state for retry; orderly exit forces a pending card flush. Memory cards still use the existing quiet-period debounce during play. Snapshot failures caused by file I/O are reported once rather than serialized repeatedly. Requests that encounter guest interrupt handlers wait for a safe point before serialization.

## Verification

Build with `PS2_BUILD_ACCURACY_TESTS=ON` and SDL2 available, then run `ctest --test-dir build/native-dmc --output-on-failure`. `native_host` uses SDL's dummy video/audio drivers and tests deadlines, device-stall policy, prebuffering, controls, focus, atomic replacement, SIO2 card read/write and failed-write recovery without game data.

`python scripts/test_native_session.py --profile --seconds 30` uses the local game/disc/checkpoint and an isolated card. It exercises the real threaded window/F5 path, compares a resumed frame byte-for-byte with uninterrupted execution, refuses a malformed state, checks fresh-process SIO2 card reads and optionally samples emulation/VU/render threads. Logs and game output stay under `games/dmc/native-stability` and remain untracked. `PS2_PACE_STATS=1` independently enables one-second field-rate, interval-percentile, queue and underrun logs.

Initial 12-second measurements reduced ordinary p95 field intervals from roughly 25–30 ms to 18–19 ms. A subsequent one-minute run stayed close to 59.94 fields/s after warmup, with the same p95 range and no further starvation after the checkpoint. Its checkpoint interval was about 112 ms and caused one recovery. The initial sampling comparison reduced SDL's render-thread sample share from 17.7% to 13.3%, and a later run measured 11.3%; background load differs, so this is directional evidence rather than a controlled CPU-speedup claim. Nominal field rates remain unchanged. Physical controller feel/listening and the game's first formatted-card save/load flow require separate validation.
