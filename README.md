# SessionUsage

Linux-only tray app forked from CodexBar. It keeps AI provider usage visible from an AppIndicator menu by pairing a native GTK tray process (`CodexBarLinux`) with the shared Swift backend (`CodexBarCLI` / `CodexBarCore`).

<img src="codexbar.png" alt="CodexBar menu screenshot" width="520" />

## Requirements
- Linux
- Swift 6
- GTK 3 + Ayatana AppIndicator development packages

## Quick start

### Install as a desktop app
```bash
sudo apt install libgtk-3-dev libayatana-appindicator3-dev libjson-glib-dev
./Scripts/install_desktop.sh
```

This builds the release binaries, installs a user-local copy into `~/.local/share/sessionusage`, creates a launcher at `~/.local/bin/sessionusage`, registers a desktop entry, and enables login autostart via `~/.config/autostart/sessionusage.desktop`.
It also starts the installed tray app right away. Pass `--no-launch` or `--no-autostart` if you want a lighter install step.

### First-time provider setup
Make sure the provider tools you care about are already installed and signed in. SessionUsage reads the same local CLI/auth/config state as the shared CodexBar backend, including `~/.codexbar/config.json` where applicable.

### Remove the desktop install
```bash
./Scripts/uninstall_desktop.sh
```

### Build from source
```bash
sudo apt install libgtk-3-dev libayatana-appindicator3-dev libjson-glib-dev
swift build --product CodexBarCLI --product CodexBarLinux
./.build/debug/CodexBarLinux
```

`CodexBarLinux` looks for `CodexBarCLI` next to the tray executable first, then falls back to `codexbar`/`CodexBarCLI` on `PATH`. You can also point it at a custom binary with `CODEXBAR_CLI=/path/to/CodexBarCLI`.
The tray consumes `CodexBarCLI --format json`, uses the project icon when available, formats provider summaries as menu sections, refreshes every two minutes, and adds native **Refresh** and **Quit** actions.

## Commands

| Command | Description |
| --- | --- |
| `./Scripts/install_desktop.sh` | Build a user-local release install, create launcher/autostart entries, and start the tray |
| `./Scripts/install_desktop.sh --no-launch --no-autostart` | Install without immediately starting the tray or enabling login autostart |
| `./Scripts/uninstall_desktop.sh` | Remove the local desktop install |
| `./Scripts/compile_and_run.sh` | Build, test, stop any existing dev tray instance, then run the debug tray |
| `swift build --product CodexBarCLI --product CodexBarLinux` | Build the CLI and Linux tray targets manually |
| `swift test` | Run the Linux-focused Swift test suite |
| `sessionusage` | Launch the installed tray app after desktop installation |

## Architecture

- `Sources/CodexBarLinux` contains the native GTK 3 + Ayatana AppIndicator tray shell.
- `Sources/CodexBarCLI` exposes structured usage output consumed by the tray.
- `Sources/CodexBarCore` contains provider integrations, auth discovery, and usage-fetch logic.
- `Assets/ProviderIcons` contains the provider logos used in the Linux tray menu.
- `Scripts/install_desktop.sh` installs a self-contained user-local app under XDG paths instead of requiring a long terminal command.

## Providers

SessionUsage keeps the shared provider backend from CodexBar, including Linux-relevant support for providers such as Claude, Copilot, Gemini, Cursor, JetBrains AI, Kilo, Kimi, OpenRouter, Augment, Amp, Warp, Ollama, and Abacus AI.

## Features
- Native Linux tray target built with GTK 3 + Ayatana AppIndicator.
- Shared Swift CLI/backend for provider polling and usage formatting.
- Tray refresh, section headers, and project icon support.
- Local-first provider integrations such as CLI, OAuth, and API-token based usage probes.

## Privacy note
Wondering if CodexBar scans your disk? It doesn’t crawl your filesystem; it reads a small set of known locations (browser cookies/local storage, local JSONL logs) when the related features are enabled. See the discussion and audit notes in [issue #12](https://github.com/steipete/CodexBar/issues/12).

## Related
- ✂️ [Trimmy](https://github.com/steipete/Trimmy) — “Paste once, run once.” Flatten multi-line shell snippets so they paste and run.
- 🧳 [MCPorter](https://mcporter.dev) — TypeScript toolkit + CLI for Model Context Protocol servers.
- 🧿 [oracle](https://askoracle.dev) — Ask the oracle when you're stuck. Invoke GPT-5 Pro with a custom context and files.

## Credits
Inspired by [ccusage](https://github.com/ryoppippi/ccusage) (MIT), specifically the cost usage tracking.

## License
MIT • Peter Steinberger ([steipete](https://twitter.com/steipete))
