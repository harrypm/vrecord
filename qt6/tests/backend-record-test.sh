#!/usr/bin/env bash
# Run the packaged vrecord backend exactly as the Qt GUI launches it, but
# headless (VRECORD_HEADLESS=1 swaps the monitor player for cat) so the
# result can be checked by machine.
#   usage: backend-record-test.sh <mode -r|-p> <config> [ID]
#   VRECORD_PKG overrides the portable package directory to test.
export PATH="/usr/bin:/ucrt64/bin:${PATH}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PKG="${VRECORD_PKG:-$(ls -td "${REPO}"/Release/artifacts/vrecord_windows_*/ 2>/dev/null | head -n1)}"
cd "${PKG}" || { echo "package not found: ${PKG}" ; exit 1 ; }
export PATH="${PKG}/bin:${PATH}"
export VRECORD_RESOURCES="${PKG}/Resources"
export VRECORD_HEADLESS=1
MODE="${1:--r}"
CONFIG="${2:-${HOME}/.vrecord.conf}"
ID="${3:-}"
echo q | timeout 180 bash ./vrecord "${MODE}" -c "${CONFIG}" ${ID:+"${ID}"} 2>&1
echo "BACKEND_EXIT=${PIPESTATUS[1]}"
