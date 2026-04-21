#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SWIFTLY_ENV="${SWIFTLY_HOME_DIR:-$HOME/.local/share/swiftly}/env.sh"
BUILD_CONFIG="release"
LAUNCH_AFTER_INSTALL=1
ENABLE_AUTOSTART=1

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"

INSTALL_ROOT="${XDG_DATA_HOME}/sessionusage"
APPLICATIONS_DIR="${XDG_DATA_HOME}/applications"
AUTOSTART_DIR="${XDG_CONFIG_HOME}/autostart"
LAUNCHER_PATH="${XDG_BIN_HOME}/sessionusage"
DESKTOP_ENTRY_PATH="${APPLICATIONS_DIR}/sessionusage.desktop"
AUTOSTART_ENTRY_PATH="${AUTOSTART_DIR}/sessionusage.desktop"
PROVIDER_ICONS_SOURCE_DIR="${ROOT_DIR}/Assets/ProviderIcons"

log()  { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Install SessionUsage as a user-local Linux tray app.

Usage:
  ./Scripts/install_desktop.sh [--debug|--release] [--no-launch] [--no-autostart]

Options:
  --debug         Install the debug build instead of release
  --release       Install the release build (default)
  --no-launch     Do not start the tray app after installation
  --no-autostart  Install the launcher without enabling login autostart
  -h, --help      Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)
            BUILD_CONFIG="debug"
            ;;
        --release)
            BUILD_CONFIG="release"
            ;;
        --no-launch)
            LAUNCH_AFTER_INSTALL=0
            ;;
        --no-autostart)
            ENABLE_AUTOSTART=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "Unknown argument: $1"
            ;;
    esac
    shift
done

if [[ -f "${SWIFTLY_ENV}" ]]; then
    # shellcheck source=/dev/null
    source "${SWIFTLY_ENV}"
    hash -r
fi

command -v swift >/dev/null 2>&1 || fail "swift is not on PATH. Load Swift first, then rerun this installer."

find_running_pids() {
    {
        pgrep -f "${ROOT_DIR}/\\.build/(debug|release|[^/]+/(debug|release))/CodexBarLinux" || true
        pgrep -f "${INSTALL_ROOT}/CodexBarLinux" || true
    } | sort -u
}

stop_running_instances() {
    mapfile -t running_pids < <(find_running_pids)
    for pid in "${running_pids[@]}"; do
        [[ -n "${pid}" ]] || continue
        kill "${pid}" 2>/dev/null || true
    done
}

write_launcher() {
    cat >"${LAUNCHER_PATH}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
export CODEXBAR_CLI="${INSTALL_ROOT}/CodexBarCLI"
export CODEXBAR_ICON="${INSTALL_ROOT}/sessionusage-symbolic.svg"
export CODEXBAR_PROVIDER_ICONS_DIR="${INSTALL_ROOT}/ProviderIcons"
exec "${INSTALL_ROOT}/CodexBarLinux"
EOF
    chmod 755 "${LAUNCHER_PATH}"
}

write_desktop_entry() {
    local destination="$1"
    local autostart_enabled="$2"
    cat >"${destination}" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=SessionUsage
Comment=Tray app for AI provider usage
Exec=${LAUNCHER_PATH}
Icon=${INSTALL_ROOT}/codexbar.png
Terminal=false
Categories=Utility;Development;
StartupNotify=false
X-GNOME-Autostart-enabled=${autostart_enabled}
EOF
}

log "==> Building CodexBarCLI (${BUILD_CONFIG})"
swift build -c "${BUILD_CONFIG}" --product CodexBarCLI

log "==> Building CodexBarLinux (${BUILD_CONFIG})"
swift build -c "${BUILD_CONFIG}" --product CodexBarLinux

BIN_SOURCE_DIR="$(swift build -c "${BUILD_CONFIG}" --show-bin-path)"
TRAY_SOURCE="${BIN_SOURCE_DIR}/CodexBarLinux"
CLI_SOURCE="${BIN_SOURCE_DIR}/CodexBarCLI"

[[ -x "${TRAY_SOURCE}" ]] || fail "CodexBarLinux binary not found at ${TRAY_SOURCE}"
[[ -x "${CLI_SOURCE}" ]] || fail "CodexBarCLI binary not found at ${CLI_SOURCE}"
[[ -f "${ROOT_DIR}/sessionusage-symbolic.svg" ]] || fail "sessionusage-symbolic.svg is missing"
[[ -f "${ROOT_DIR}/codexbar.png" ]] || fail "codexbar.png is missing"
[[ -d "${PROVIDER_ICONS_SOURCE_DIR}" ]] || fail "Assets/ProviderIcons is missing"

log "==> Installing into ${INSTALL_ROOT}"
mkdir -p "${INSTALL_ROOT}" "${INSTALL_ROOT}/ProviderIcons" "${APPLICATIONS_DIR}" "${AUTOSTART_DIR}" "${XDG_BIN_HOME}"

install -m 755 "${TRAY_SOURCE}" "${INSTALL_ROOT}/CodexBarLinux"
install -m 755 "${CLI_SOURCE}" "${INSTALL_ROOT}/CodexBarCLI"
install -m 644 "${ROOT_DIR}/sessionusage-symbolic.svg" "${INSTALL_ROOT}/sessionusage-symbolic.svg"
install -m 644 "${ROOT_DIR}/codexbar.png" "${INSTALL_ROOT}/codexbar.png"
find "${INSTALL_ROOT}/ProviderIcons" -mindepth 1 -maxdepth 1 -type f -delete
find "${PROVIDER_ICONS_SOURCE_DIR}" -mindepth 1 -maxdepth 1 -type f -exec install -m 644 "{}" "${INSTALL_ROOT}/ProviderIcons/" \;

write_launcher
write_desktop_entry "${DESKTOP_ENTRY_PATH}" "false"
if [[ "${ENABLE_AUTOSTART}" -eq 1 ]]; then
    write_desktop_entry "${AUTOSTART_ENTRY_PATH}" "true"
else
    rm -f "${AUTOSTART_ENTRY_PATH}"
fi

update-desktop-database "${APPLICATIONS_DIR}" >/dev/null 2>&1 || true

if [[ "${LAUNCH_AFTER_INSTALL}" -eq 1 ]]; then
    log "==> Restarting SessionUsage"
    stop_running_instances
    if command -v setsid >/dev/null 2>&1; then
        setsid -f "${LAUNCHER_PATH}" >/dev/null 2>&1 || true
    else
        nohup "${LAUNCHER_PATH}" >/dev/null 2>&1 &
    fi
    sleep 1
    if ! pgrep -f "${INSTALL_ROOT}/CodexBarLinux" >/dev/null 2>&1; then
        fail "Installation finished, but the tray app did not stay running."
    fi
fi

log
log "Installed SessionUsage:"
log "  App files:     ${INSTALL_ROOT}"
log "  Launcher:      ${LAUNCHER_PATH}"
log "  Desktop entry: ${DESKTOP_ENTRY_PATH}"
if [[ "${ENABLE_AUTOSTART}" -eq 1 ]]; then
    log "  Autostart:     ${AUTOSTART_ENTRY_PATH}"
else
    log "  Autostart:     disabled"
fi
