#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRAY_BIN="${ROOT_DIR}/.build/debug/CodexBarLinux"
CLI_BIN="${ROOT_DIR}/.build/debug/CodexBarCLI"
SWIFTLY_ENV="${SWIFTLY_HOME_DIR:-$HOME/.local/share/swiftly}/env.sh"

log()  { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

if [[ -f "${SWIFTLY_ENV}" ]]; then
  # shellcheck source=/dev/null
  source "${SWIFTLY_ENV}"
  hash -r
fi

find_running_pids() {
  pgrep -f "${ROOT_DIR}/\\.build/(debug|release)/CodexBarLinux" || true
}

stop_running_instances() {
  mapfile -t pids < <(find_running_pids | sort -u)
  if [[ "${#pids[@]}" -eq 0 ]]; then
    return 0
  fi

  log "==> Stopping existing CodexBarLinux instances"
  for pid in "${pids[@]}"; do
    kill "${pid}" 2>/dev/null || true
  done

  for _ in {1..20}; do
    mapfile -t pids < <(find_running_pids | sort -u)
    if [[ "${#pids[@]}" -eq 0 ]]; then
      return 0
    fi
    sleep 0.2
  done

  for pid in "${pids[@]}"; do
    kill -9 "${pid}" 2>/dev/null || true
  done
}

log "==> Building CodexBarCLI + CodexBarLinux"
swift build --product CodexBarCLI --product CodexBarLinux

log "==> Running tests"
swift test

[[ -x "${TRAY_BIN}" ]] || fail "CodexBarLinux binary not found at ${TRAY_BIN}"
[[ -x "${CLI_BIN}" ]] || fail "CodexBarCLI binary not found at ${CLI_BIN}"

stop_running_instances

log "==> Launching CodexBarLinux"
CODEXBAR_CLI="${CLI_BIN}" exec "${TRAY_BIN}"
