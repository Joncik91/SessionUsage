#!/usr/bin/env bash

set -euo pipefail

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
XDG_BIN_HOME="${XDG_BIN_HOME:-$HOME/.local/bin}"

INSTALL_ROOT="${XDG_DATA_HOME}/sessionusage"
APPLICATIONS_DIR="${XDG_DATA_HOME}/applications"
AUTOSTART_DIR="${XDG_CONFIG_HOME}/autostart"
LAUNCHER_PATH="${XDG_BIN_HOME}/sessionusage"
DESKTOP_ENTRY_PATH="${APPLICATIONS_DIR}/sessionusage.desktop"
AUTOSTART_ENTRY_PATH="${AUTOSTART_DIR}/sessionusage.desktop"

log()  { printf '%s\n' "$*"; }

find_running_pids() {
    pgrep -f "${INSTALL_ROOT}/SessionUsage" || true
}

mapfile -t running_pids < <(find_running_pids | sort -u)
for pid in "${running_pids[@]}"; do
    [[ -n "${pid}" ]] || continue
    kill "${pid}" 2>/dev/null || true
done

rm -f "${LAUNCHER_PATH}" "${DESKTOP_ENTRY_PATH}" "${AUTOSTART_ENTRY_PATH}"
rm -rf "${INSTALL_ROOT}"
update-desktop-database "${APPLICATIONS_DIR}" >/dev/null 2>&1 || true

log "Removed SessionUsage desktop install."
