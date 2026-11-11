#!/usr/bin/env bash
# Build an NSIS installer from the portable Windows package.
# Requires: portable package already built (build-windows-portable.sh)
#           and makensis on PATH (choco install nsis / MSYS package).

set -euo pipefail
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ROOT="$(cd .. && pwd)"
VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' "$ROOT/vrecord")"
ARTIFACTS="$RELEASE_DIR/artifacts"
PORTABLE="$ARTIFACTS/vrecord_windows_${VERSION}"
if [[ ! -d "$PORTABLE" ]]; then
  # fall back to unversioned name used during development
  PORTABLE="$ARTIFACTS/vrecord_windows_portable"
fi
if [[ ! -d "$PORTABLE" ]]; then
  echo "ERROR: portable package not found. Run build-windows-portable.sh first." >&2
  exit 1
fi

NSI="$ARTIFACTS/vrecord_installer.nsi"
cat > "$NSI" << EOF
!define VERSION "${VERSION}"
Name "vrecord"
OutFile "vrecord_\${VERSION}_setup.exe"
InstallDir "\$PROGRAMFILES64\\vrecord"
RequestExecutionLevel admin
Page directory
Page instfiles
Section "Install"
  SetOutPath "\$INSTDIR"
  File /r "${PORTABLE}\\*.*"
  CreateDirectory "\$SMPROGRAMS\\vrecord"
  CreateShortCut "\$SMPROGRAMS\\vrecord\\vrecord.lnk" "\$INSTDIR\\vrecord.cmd" "" "\$INSTDIR\\vrecord.cmd" 0
  CreateShortCut "\$SMPROGRAMS\\vrecord\\Uninstall.lnk" "\$INSTDIR\\uninstall.exe"
  WriteUninstaller "\$INSTDIR\\uninstall.exe"
SectionEnd
Section "Uninstall"
  Delete "\$INSTDIR\\uninstall.exe"
  RMDir /r "\$INSTDIR"
  Delete "\$SMPROGRAMS\\vrecord\\vrecord.lnk"
  Delete "\$SMPROGRAMS\\vrecord\\Uninstall.lnk"
  RMDir "\$SMPROGRAMS\\vrecord"
SectionEnd
EOF

if command -v makensis >/dev/null 2>&1; then
  (cd "$ARTIFACTS" && makensis "$(basename "$NSI")")
  echo "Built installer in $ARTIFACTS"
else
  echo "makensis not found — wrote $NSI only."
  echo "Install NSIS (e.g. choco install nsis) and re-run."
fi
