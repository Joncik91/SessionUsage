#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TRAY_BIN="${PROJECT_ROOT}/.build/debug/SessionUsage"
CLI_BIN="${PROJECT_ROOT}/.build/debug/SessionUsageCLI"
SWIFTLY_ENV="${SWIFTLY_HOME_DIR:-$HOME/.local/share/swiftly}/env.sh"

if [[ -f "${SWIFTLY_ENV}" ]]; then
    # shellcheck source=/dev/null
    source "${SWIFTLY_ENV}"
    hash -r
fi

find_running_pids() {
    pgrep -f "${PROJECT_ROOT}/\\.build/(debug|release)/SessionUsage" || true
}

mapfile -t running_pids < <(find_running_pids | sort -u)
for pid in "${running_pids[@]}"; do
    kill "${pid}" 2>/dev/null || true
done

if [[ ! -x "${TRAY_BIN}" || ! -x "${CLI_BIN}" ]]; then
    echo "ERROR: Linux binaries are missing."
    echo "Run ./Scripts/compile_and_run.sh first."
    exit 1
fi

echo "==> Launching SessionUsage"
SESSIONUSAGE_CLI="${CLI_BIN}" exec "${TRAY_BIN}"
