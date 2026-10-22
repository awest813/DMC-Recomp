# Browser player

Open the game through the launcher in Browser mode. The player has a ready screen, a game frame, and a toolbar for sound, controls, settings and fullscreen.

- Settings contains volume, picture quality, screen shape, memory-card backups and optional performance details. Volume and mute take effect immediately and stay in this browser.
- Picture changes wait for **Apply & restart**. Restart returns to the session's starting point, including its launch checkpoint when present. Save through the game's menu first.
- Controls shows the keyboard mappings supplied by the launcher. Alt+Enter or a double-click toggles fullscreen; Escape exits. F remains available for the right stick.
- Menus release held game inputs and suppress keyboard, touch and controller input while open. The game continues running; these panels do not pause emulation.
- Browser memory cards belong to the browser origin. Export a backup before clearing browser storage. Imports replace slot 1 and reload only after the card is stored successfully.
- Touch devices show on-screen controls, with a toolbar toggle to hide them.

The audio output gain does not alter emulated SPU2 samples. The worklet retains its 60 ms startup/recovery buffer and 5 ms fades. If audio startup fails, gameplay can continue muted and the sound button retries audio initialization.

## Checks

`node scripts/test_web_audio.mjs` tests the actual worklet's buffering and sample behavior.

`node scripts/test_web_startup.mjs` executes the actual startup hook with storage/network failures and verifies that preparation dependencies are released in the correct order.

`node scripts/test_web_ui.mjs <page-url> [debug-port]` exercises the built player through an already running, isolated headless Chrome with CDP enabled. Supply a URL with `state=ground17000.ps2` and a local disc server. It checks ready state, dialogs, phone-sized layouts, pending picture settings, serialized storage callbacks, volume, mute, audio suspension/retry, controller input isolation, keyboard focus and advancing gameplay. It writes local ready/settings captures under `build/`; game data and captures must remain untracked.
