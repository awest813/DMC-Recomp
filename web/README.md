# Browser player

Open the game through the launcher in Browser mode. The player has a ready screen, a game frame, and a toolbar for sound, controls, settings and fullscreen.

- Settings contains volume, picture quality, screen shape, memory-card backups and optional performance details. Volume and mute take effect immediately and stay in this browser.
- Picture changes wait for **Apply & restart**. Restart returns to the session's starting point, including its launch checkpoint when present. Save through the game's menu first.
- Controls shows the keyboard mappings supplied by the launcher. Alt+Enter or a double-click toggles fullscreen; Escape exits. F remains available for the right stick.
- Menus release held game inputs and suppress keyboard, touch and controller input while open. The game continues running; these panels do not pause emulation.
- Browser memory cards belong to the browser origin. Export a backup before clearing browser storage. Imports replace slot 1 and reload only after the card is stored successfully.
- Touch devices show on-screen controls, with a toolbar toggle to hide them.

The audio output gain does not alter emulated SPU2 samples. The worklet retains its 60 ms startup/recovery buffer and 5 ms fades. If audio startup fails, gameplay can continue muted and the sound button retries audio initialization.

## GitHub Pages ISO picker

The [hosted launcher](https://awest813.github.io/DMC-Recomp/) includes the browser player. Choose a local Devil May Cry NTSC-U (`SLUS_202.16`) ISO, then **Open game**. There is no build-file picker and no local server or compilation step. The four included player files download automatically (about 31 MB).

The launcher checks the ISO's `SYSTEM.CNF` for `SLUS_202.16` with bounded metadata reads. A service worker serves local disc slices and the isolation headers required by the player's threads. It forwards disc requests to the open launcher tab so the ISO survives service worker restarts without copying a multi-gigabyte image into browser storage. The ISO is not uploaded, cached or persisted by the launcher. Reloading requires selecting it again. Closing a game invalidates its disc URL; close the tab to release the selected ISO.

`.github/workflows/pages.yml` checks disc handling and runs `scripts/package_pages.py`. Packaging copies an explicit list of launcher files and downloads exactly four player artifacts from the release pinned in `web/pages/runtime.json`, verifying their sizes and SHA-256 hashes before deploying `build/pages`. Configure the repository's Pages publishing source as **GitHub Actions**. This uses GitHub's [custom Pages workflow](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) support.

The user authorized publication of `ps2web.html`, `ps2web.js`, `ps2web.wasm` and `ps2web.data`, including the recompiled game code and packed boot executable. They remain release assets rather than source commits. No ISO, generated C++ source, state, card, audio or capture is bundled. To update the player, build locally, test it, publish a new versioned release containing those four files and update their hashes and sizes in the manifest. Never overwrite a pinned release in place.

Use current desktop Chrome or Edge with WebAssembly JSPI; WebGPU is recommended. The worker is scoped to the repository's Pages directory, and its first visit reloads once to enable isolation. Browser cards belong to the origin and are separate from localhost cards. Use the player's memory-card import/export controls to transfer saves.

For a header-free local test, run `python scripts/package_pages.py` (or add `--runtime-dir build/web-dmc` to use matching local artifacts), then `python -m http.server 8630 --directory build` and open `http://localhost:8630/pages/`. The output directory must not already exist; use `--output` to choose a fresh path. `node scripts/test_pages.mjs` checks disc validation and range responses without game data. With an isolated Chrome CDP session, run `node scripts/test_pages_browser.mjs <picker-url> <local-iso> [cdp-port]` to check isolation, subpath hosting, worker restart, ISO-only selection, real gameplay and session cleanup.

## Checks

`node scripts/test_web_audio.mjs` tests the actual worklet's buffering and sample behavior.

`node scripts/test_web_startup.mjs` executes the actual startup hook with storage/network failures and verifies that preparation dependencies are released in the correct order.

`node scripts/test_web_ui.mjs <page-url> [debug-port]` exercises the built player through an already running, isolated headless Chrome with CDP enabled. Supply a URL with `state=ground17000.ps2` and a local disc server. It checks ready state, dialogs, phone-sized layouts, pending picture settings, serialized storage callbacks, volume, mute, audio suspension/retry, controller input isolation, keyboard focus and advancing gameplay. It writes local ready/settings captures under `build/`; game data and captures must remain untracked.
