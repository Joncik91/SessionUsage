#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="${ROOT_DIR}/.build/lint-tools/bin"
FORMAT_PATHS=(Sources TestsLinux)
SWIFTLY_ENV="${SWIFTLY_HOME_DIR:-$HOME/.local/share/swiftly}/env.sh"

if [[ -f "${SWIFTLY_ENV}" ]]; then
  # SwiftLint on Linux needs the toolchain's SourceKit shared library on the loader path.
  # Swiftly exposes the active toolchain through env.sh.
  # shellcheck source=/dev/null
  source "${SWIFTLY_ENV}"
  hash -r
fi

if [[ "$(uname -s)" == "Linux" ]] && command -v swiftc >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
  RUNTIME_RESOURCE_PATH="$(
    swiftc -print-target-info | python3 -c 'import json, sys; print(json.load(sys.stdin)["paths"]["runtimeResourcePath"])'
  )"
  SOURCEKIT_LIBRARY_PATH="$(cd "${RUNTIME_RESOURCE_PATH}/.." && pwd)"
  export LD_LIBRARY_PATH="${SOURCEKIT_LIBRARY_PATH}:${LD_LIBRARY_PATH:-}"
  export LINUX_SOURCEKIT_LIB_PATH="${SOURCEKIT_LIBRARY_PATH}"
fi

ensure_tools() {
  # Always delegate to the installer so pinned versions are enforced.
  # The installer is idempotent and exits early when the expected versions are already present.
  "${ROOT_DIR}/Scripts/install_lint_tools.sh"
}

cmd="${1:-lint}"

case "$cmd" in
  lint)
    ensure_tools
    "${BIN_DIR}/swiftformat" "${FORMAT_PATHS[@]}" --lint
    "${BIN_DIR}/swiftlint" --strict
    ;;
  format)
    ensure_tools
    "${BIN_DIR}/swiftformat" "${FORMAT_PATHS[@]}"
    ;;
  *)
    printf 'Usage: %s [lint|format]\n' "$(basename "$0")" >&2
    exit 2
    ;;
esac
