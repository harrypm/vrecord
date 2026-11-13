#!/usr/bin/env bash
# Build the Windows installer (NSIS) from the assembled portable package.
#   bash Release/build-windows-installer.sh            (after build-windows-portable.sh)
# Needs makensis (MSYS2: pacman -S mingw-w64-ucrt-x86_64-nsis).
# Output: Release/artifacts/vrecord_<VERSION>_windows_setup.exe

set -euo pipefail
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ROOT="$(cd .. && pwd)"
VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' "$ROOT/vrecord")"
# overridable (tests build from a copy of the package)
PKG="${VRECORD_PKG_DIR:-$RELEASE_DIR/artifacts/vrecord_windows_${VERSION}}"
OUT="${VRECORD_INSTALLER_OUT:-$RELEASE_DIR/artifacts/vrecord_${VERSION}_windows_setup.exe}"

[[ -f "$PKG/VrecordQt6.exe" ]] || { echo "no package at $PKG - run build-windows-portable.sh first" >&2 ; exit 1 ; }
command -v makensis > /dev/null || { echo "makensis not found (pacman -S mingw-w64-ucrt-x86_64-nsis)" >&2 ; exit 1 ; }

# VIProductVersion needs four numbers: 2026-03-03 -> 2026.3.3.0
IFS=' ' read -r -a PARTS <<< "$(echo "$VERSION" | grep -oE '[0-9]+' | sed 's/^0*\([0-9]\)/\1/' | tr '\n' ' ')"
while [[ ${#PARTS[@]} -lt 4 ]] ; do PARTS+=(0) ; done
VIVERSION="${PARTS[0]}.${PARTS[1]}.${PARTS[2]}.${PARTS[3]}"

win() { cygpath -w "$1" ; }
rm -f "$OUT"
echo "Building the installer for vrecord ${VERSION} (${VIVERSION})"
# MSYS2_ARG_CONV_EXCL: keep makensis's /D options from being taken for paths
MSYS2_ARG_CONV_EXCL='*' makensis -V2 -INPUTCHARSET UTF8 \
    "-DVERSION=${VERSION}" "-DVIVERSION=${VIVERSION}" \
    "-DPKGDIR=$(win "$PKG")" "-DOUTFILE=$(win "$OUT")" \
    "-DICON=$(win "$RELEASE_DIR/vrecord.ico")" "-DLICENSE=$(win "$ROOT/LICENSE.txt")" \
    "$(win "$RELEASE_DIR/windows-installer.nsi")"
[[ -f "$OUT" ]] || { echo "makensis produced no installer" >&2 ; exit 1 ; }
echo "Installer: $OUT ($(du -h "$OUT" | cut -f1))"
