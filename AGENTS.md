# Repository Guidelines

## Project Structure & Modules
- `Sources/SessionUsage` contains the Linux GTK/AppIndicator tray shell.
- `Sources/SessionUsageCLI` contains the Swift CLI surface that emits the structured payloads consumed by the tray.
- `Sources/CodexBarCore` contains the shared provider/auth/config logic. Internal type names still use CodexBar in places; treat that as shared-core legacy, not a cue to reintroduce old branding in new public surfaces.
- `TestsLinux` contains the active Swift Testing suite for the Linux fork.
- `Scripts` contains the supported local workflows (`compile_and_run.sh`, `launch.sh`, `install_desktop.sh`, `uninstall_desktop.sh`, lint helpers).

## Build, Test, Run
- Preferred dev loop: `./Scripts/compile_and_run.sh`
- Manual build: `swift build --product SessionUsageCLI --product SessionUsage`
- Tests: `swift test`
- Desktop install: `./Scripts/install_desktop.sh`
- Desktop uninstall: `./Scripts/uninstall_desktop.sh`

## Coding Style & Naming
- Keep changes small and reuse existing helpers before adding new abstractions.
- Follow the existing Swift formatting and explicit-`self` style in shared code.
- Prefer `SessionUsage` naming in user-facing surfaces, docs, scripts, env vars, and binaries.
- Preserve legacy `CODEXBAR_*` / `~/.codexbar` compatibility when touching existing runtime/config paths unless a task explicitly removes compatibility.

## Testing Guidelines
- Add focused Swift Testing coverage in `TestsLinux` for behavior changes.
- Prefer testing structured seams and pure logic over fragile desktop-environment wiring.
- Run the existing build/test flow after code changes before handoff.

## Agent Notes
- This repo is Linux-first. Do not restore macOS app packaging, appcast, notarization, or Sparkle-era workflows unless explicitly asked.
- Keep the tray shell presentation-focused; provider probing and parsing belong in Swift shared code.
- When changing the tray runtime, preserve fallback behavior for legacy CLI/config/env names unless the task says otherwise.
