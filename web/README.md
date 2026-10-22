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

The [hosted launcher](https://awest813.github.io/DMC-Recomp/) accepts a local NTSC-U DMC ISO and the four files from a local browser build: `ps2web.html`, `ps2web.js`, `ps2web.wasm` and `ps2web.data`. Build them with `scripts/build_web.ps1 dmc` first. An ISO alone is insufficient: this project compiles the game ahead of time, and the resulting game code and packaged executable remain local under the repository's publication rules.

The launcher checks the ISO's `SYSTEM.CNF` for `SLUS_202.16` with bounded metadata reads. A service worker serves local file slices to the player, including HTTP byte ranges and the isolation headers required by its threads. It forwards requests to the open launcher tab so disc files survive service worker restarts without copying a multi-gigabyte ISO into browser storage. Files are not uploaded, cached or persisted by the launcher. Reloading requires selecting them again. Closing a game invalidates its session URLs; close the tab to release the selected files.

Pages hosts only the authored static files in `web/pages/`. `.github/workflows/pages.yml` checks disc handling and deploys that directory on changes to `dmc-port` or manual dispatch. Configure the repository's Pages publishing source as **GitHub Actions**. No generated game code, builds, ISO, states or captures are deployed. This uses GitHub's [custom Pages workflow](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) support.

Use current desktop Chrome or Edge with WebAssembly JSPI; WebGPU is recommended. The worker is scoped to the repository's Pages directory, and its first visit reloads once to enable isolation. Browser cards belong to the origin and are separate from localhost cards. Use the player's memory-card import/export controls to transfer saves.

For a header-free local test, run `python -m http.server 8630 --directory web` and open `http://localhost:8630/pages/`. `node scripts/test_pages.mjs` checks disc validation and range responses without game data. With an isolated Chrome CDP session, run `node scripts/test_pages_browser.mjs <picker-url> <local-iso> <web-build-dir> [cdp-port]` to check isolation, subpath hosting, worker restart, file selection, real gameplay and session cleanup.

## Checks

`node scripts/test_web_audio.mjs` tests the actual worklet's buffering and sample behavior.

`node scripts/test_web_startup.mjs` executes the actual startup hook with storage/network failures and verifies that preparation dependencies are released in the correct order.

`node scripts/test_web_ui.mjs <page-url> [debug-port]` exercises the built player through an already running, isolated headless Chrome with CDP enabled. Supply a URL with `state=ground17000.ps2` and a local disc server. It checks ready state, dialogs, phone-sized layouts, pending picture settings, serialized storage callbacks, volume, mute, audio suspension/retry, controller input isolation, keyboard focus and advancing gameplay. It writes local ready/settings captures under `build/`; game data and captures must remain untracked.
