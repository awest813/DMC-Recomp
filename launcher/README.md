# Devil May Cry launcher

Open **Play Devil May Cry.bat** from the repository root.

- **Play:** choose Window or Browser, then Start game. Resume quick save loads the last native F5 snapshot; its timestamp appears below the buttons.
- **Settings:** select your US-release disc image, a window memory card, picture quality, fullscreen and aspect ratio. Settings are remembered between launches. Use 1x if playback struggles; 2x is the default.
- **Controls & help:** view default controls, customize the keyboard, restore defaults, or open the window game's log. The keyboard editor scrolls on smaller screens and checks key names before saving.

Window memory cards and browser cards are separate. Browser cards remain on the original `http://localhost:8620` origin; import/export them inside the game. Quick saves are runtime snapshots and do not replace in-game memory-card saves.

The launcher checks setup before enabling Play. Window games launch directly, with output in `games/dmc/launcher-game.log`. Browser play checks both reused and newly started servers against the selected build/disc at the browser's actual localhost address. A failed session does not overwrite its served checkpoint. The launcher stops only servers it started and asks before closing an active owned server. An older server without the session-identity endpoint must be closed before this launcher can use its address.

## Development checks

```powershell
powershell -NoProfile -File scripts/test_launcher.ps1
python scripts/test_serve.py
powershell -NoProfile -File launcher/launcher.ps1 -Play native -DryRun
powershell -NoProfile -File launcher/launcher.ps1 -Play browser -DryRun -Resume
powershell -NoProfile -File launcher/launcher.ps1 -Snapshot C:\path\launcher.png -SnapshotPage Settings
```

Snapshot pages are Play, Settings, Help and Controls. Dry runs and snapshots do not save settings or controls, start a server, or launch a game. `-SettingsFile`, `-Disc`, `-MemoryCard` and `-Scale` support isolated configuration checks. Launch tests mock the actual process start; the local server integration test uses a small fixture and verifies identity, disc ranges and cross-origin isolation headers. Game data and UI captures stay untracked.
