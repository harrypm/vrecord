#!/usr/bin/env bash
# Assemble a self-contained Windows portable package for vrecord.
# Expects prior builds under VRECORD_BUILD (default: $HOME/vrecord-build):
#   ffmpeg-install/, gtkdialog-install/, xmlstarlet-install/,
#   decklink-include/, mediainfo-cli/, mkvtoolnix/, dejavu fonts, cowsay/
#
# Output: Release/artifacts/vrecord_windows_<VERSION>/ + .zip

set -euo pipefail
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ROOT="$(cd .. && pwd)"
VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' "$ROOT/vrecord")"
BUILD_DIR="${VRECORD_BUILD:-$HOME/vrecord-build}"
ICON_RC="$RELEASE_DIR/appicon.rc"
ICON_ICO="$RELEASE_DIR/vrecord.ico"
# On Windows MSYS this is typically /c/Users/.../vrecord-build
if [[ ! -d "$BUILD_DIR" && -d /c/Users/Harry/vrecord-build ]]; then
  BUILD_DIR=/c/Users/Harry/vrecord-build
fi
ARTIFACTS="$RELEASE_DIR/artifacts"
PKG="$ARTIFACTS/vrecord_windows_${VERSION}"
rm -rf "$PKG"
mkdir -p "$PKG/bin" "$PKG/Resources/fonts" "$ARTIFACTS"

echo "Packaging vrecord Windows portable ${VERSION}"
echo "  build dir: $BUILD_DIR"

cp "$ROOT/vrecord" "$ROOT/vtest" "$ROOT/vplay" "$PKG/" 2>/dev/null || true
chmod 755 "$PKG/vrecord" "$PKG/vtest" "$PKG/vplay" 2>/dev/null || true
cp -a "$ROOT/Resources/." "$PKG/Resources/"

# build Qt6 frontend when tooling is available
QT_BUILD_DIR="$ROOT/qt6/build"
if [[ -f "$ROOT/qt6/CMakeLists.txt" ]] && command -v cmake >/dev/null 2>&1 ; then
  rm -rf "$QT_BUILD_DIR"
  cmake -S "$ROOT/qt6" -B "$QT_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DDECKLINK_INCLUDE_DIR="$BUILD_DIR/decklink-include" \
    -DVRECORD_UPDATE_REPO="${VRECORD_UPDATE_REPO:-harrypm/vrecord}"
  cmake --build "$QT_BUILD_DIR" --config Release
  if [[ -f "$QT_BUILD_DIR/VrecordQt6.exe" ]]; then
    cp -f "$QT_BUILD_DIR/VrecordQt6.exe" "$PKG/"
    if [[ -f "$QT_BUILD_DIR/vsignal.exe" ]]; then
      cp -f "$QT_BUILD_DIR/vsignal.exe" "$PKG/"
    fi
    if command -v windeployqt6.exe >/dev/null 2>&1 ; then
      windeployqt6.exe --no-compiler-runtime --dir "$PKG" "$PKG/VrecordQt6.exe" >/dev/null 2>&1 || true
    elif command -v windeployqt.exe >/dev/null 2>&1 ; then
      windeployqt.exe --no-compiler-runtime --dir "$PKG" "$PKG/VrecordQt6.exe" >/dev/null 2>&1 || true
    fi
    # headless platform plugin: lets `QT_QPA_PLATFORM=offscreen VrecordQt6.exe
    # --self-test` run without a desktop (CI); windeployqt only adds qwindows
    for QT_PLUGIN_ROOT in /ucrt64/share/qt6/plugins /ucrt64/lib/qt6/plugins ; do
      if [[ -f "$QT_PLUGIN_ROOT/platforms/qoffscreen.dll" ]]; then
        mkdir -p "$PKG/platforms"
        cp -f "$QT_PLUGIN_ROOT/platforms/qoffscreen.dll" "$PKG/platforms/"
        break
      fi
    done
  fi
fi

# native Windows launchers
export PATH="/ucrt64/bin:${PATH}"
if command -v windres >/dev/null 2>&1 && command -v gcc >/dev/null 2>&1; then
  if [[ -f "$ICON_ICO" && -f "$ICON_RC" ]]; then
    windres "$ICON_RC" -O coff -o "$RELEASE_DIR/appicon.res"
    gcc -O2 -mwindows -o "$PKG/Vrecord.exe" "$RELEASE_DIR/vrecord-launcher.c" "$RELEASE_DIR/appicon.res"
    rm -f "$RELEASE_DIR/appicon.res"
  else
    gcc -O2 -mwindows -o "$PKG/Vrecord.exe" "$RELEASE_DIR/vrecord-launcher.c"
  fi
  gcc -O2 -mwindows -o "$PKG/bin/bash.exe" "$RELEASE_DIR/vrecord-bashwrap.c"
fi

# fonts
if [[ -d "$BUILD_DIR/dejavu-fonts" ]]; then
  find "$BUILD_DIR/dejavu-fonts" -name 'DejaVuSans*.ttf' -exec cp -f {} "$PKG/Resources/fonts/" \;
fi

# core binaries
cp -f "$BUILD_DIR/ffmpeg-install/bin/ffmpeg-ma.exe" \
      "$BUILD_DIR/ffmpeg-install/bin/ffplay-ma.exe" \
      "$BUILD_DIR/ffmpeg-install/bin/ffprobe-ma.exe" "$PKG/bin/"
cp -f "$BUILD_DIR/ffmpeg-install/bin/"*.dll "$PKG/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/gtkdialog-install/bin/gtkdialog.exe" "$PKG/bin/"
cp -f "$BUILD_DIR/xmlstarlet-install/bin/xmlstarlet.exe" "$PKG/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/xmlstarlet-install/bin/xml.exe" "$PKG/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/mediainfo-cli/MediaInfo.exe" "$BUILD_DIR/mediainfo-cli/"*.dll "$PKG/bin/" 2>/dev/null || true
# optional tools (the GTK "Optional Tools" lights / Qt "Installed components"):
# deckcontrol (RS-422 deck control, built from deckcontrol-src against the
# DeckLink SDK), MediaConch CLI (+ its libcurl), DVRescue CLI, mpv (QC view)
cp -f "$BUILD_DIR/deckcontrol-src/deckcontrol.exe" "$PKG/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/mediaconch-x/MediaConch.exe" "$BUILD_DIR/mediaconch-x/LIBCURL.DLL" "$PKG/bin/" 2>/dev/null || true
cp -f "$BUILD_DIR/dvrescue-x/DVRescue.exe" "$PKG/bin/" 2>/dev/null || true
# dvpackager: vendored + patched for Windows (Release/vendor/dvpackager/PATCHES.md)
cp -f "$RELEASE_DIR/vendor/dvpackager/dvpackager" "$PKG/bin/dvpackager"
cp -f "$RELEASE_DIR/vendor/dvpackager/LICENSE.txt" "$PKG/bin/dvpackager-LICENSE.txt"
cp -f "$BUILD_DIR/mpv-x/mpv.exe" "$BUILD_DIR/mpv-x/mpv.com" "$BUILD_DIR/mpv-x/d3dcompiler_43.dll" "$PKG/bin/" 2>/dev/null || true
find "$BUILD_DIR/mkvtoolnix" -name mkvpropedit.exe -exec cp -f {} "$PKG/bin/" \; 2>/dev/null || true
find "$BUILD_DIR/mkvtoolnix" -name mkvmerge.exe -exec cp -f {} "$PKG/bin/" \; 2>/dev/null || true
if [[ -d "$BUILD_DIR/cowsay" ]]; then
  mkdir -p "$PKG/share/cowsay"
  cp -a "$BUILD_DIR/cowsay/bin/"* "$PKG/bin/" 2>/dev/null || true
  cp -a "$BUILD_DIR/cowsay/share/cowsay/"* "$PKG/share/cowsay/" 2>/dev/null || true
fi

# transitive UCRT DLLs
export PATH="/ucrt64/bin:${PATH}"
collect_once() {
  local changed=0
  local bin lib base target_dir
  for bin in "$PKG/"* "$PKG/bin/"*; do
    [[ -f "$bin" ]] || continue
    target_dir="$(dirname "$bin")"
    while read -r lib; do
      [[ -f "$lib" ]] || continue
      base="$(basename "$lib")"
      if [[ ! -f "$target_dir/$base" ]]; then
        cp -f "$lib" "$target_dir/"
        changed=1
      fi
    done < <(ldd "$bin" 2>/dev/null | awk '/=> \/ucrt64\// { print $3 }')
  done
  echo "$changed"
}
for _ in 1 2 3 4 5; do
  [[ "$(collect_once)" = 0 ]] && break
done

cat > "$PKG/vrecord.sh" << 'EOF'
#!/usr/bin/env bash
ROOT="$(cd "$(dirname "$0")" && pwd)"
export PATH="$ROOT/bin:$PATH"
export VRECORD_RESOURCES="$ROOT/Resources"
cd "$ROOT"
exec bash ./vrecord "$@"
EOF
chmod +x "$PKG/vrecord.sh"

cat > "$PKG/vrecord.cmd" << 'EOF'
@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "PATH=%ROOT%\bin;%PATH%"
set "VRECORD_RESOURCES=%ROOT%\Resources"
set "BASH="
if exist "C:\msys64\usr\bin\bash.exe" set "BASH=C:\msys64\usr\bin\bash.exe"
if not defined BASH if exist "%ProgramFiles%\Git\bin\bash.exe" set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not defined BASH (
  echo ERROR: Need MSYS2 bash (C:\msys64) or Git Bash to run vrecord on Windows.
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%BASH%" -lc "cygpath -u '%ROOT%'" 2^>nul`) do set "MROOT=%%I"
if not defined MROOT (
  echo ERROR: could not convert package path for bash
  exit /b 1
)
"%BASH%" -lc "export PATH=\"$MROOT/bin:$PATH\"; export VRECORD_RESOURCES=\"$MROOT/Resources\"; cd \"$MROOT\"; exec bash ./vrecord \"$@\"" -- %*
exit /b %ERRORLEVEL%
EOF

cat > "$PKG/README-WINDOWS.txt" << EOF
vrecord Windows portable ${VERSION}
===================================

Requirements:
  - Blackmagic Desktop Video drivers installed (this station: 16.0.x)
  - MSYS2 bash at C:\\msys64 OR Git Bash

Run:
  VrecordQt6.exe
  vsignal start --pattern "SMPTE SD Bars (75%)" --connector HDMI
  vsignal stop
  Vrecord.exe
  vrecord.cmd
  vrecord.cmd -e
  vrecord.cmd -s testid
  vrecord.cmd mytape

Install or portable:
  - vrecord_<version>_windows_x86_64_setup.exe installs for the current user
    (no administrator rights) into %LOCALAPPDATA%\\Programs\\vrecord, with
    Start Menu shortcuts and an Apps & features entry.
  - vrecord_<version>_windows_x86_64_portable.zip runs from any folder
    (portable.txt marks it as portable).

Updates (VrecordQt6 > Config > Updates):
  - Off / tell me when an update is available (default) / download
    automatically and install when vrecord closes. "Check for updates" and
    "Install and restart" work in every mode.
  - Only published GitHub releases are offered. Every download is checked
    against the release's SHA256SUMS.txt and refused when it does not match.
  - Installed copies run the new setup silently; portable copies unpack the
    new zip over this folder (files the zip does not carry, such as your
    logs, stay). Nothing is installed while a recording is running.
  - The install helper's log: %TEMP%\\vrecord-update\\<tag>\\apply-update.log

Notes:
  - Output directories must be normal Windows paths (e.g. C:/Captures).
  - ffmpeg-ma is a MinGW build with decklink indev/outdev and drawtext
    (freetype/harfbuzz) enabled - see Release/build-ffmpeg-windows.sh.
  - gtkdialog GUI is GTK3-based; preview uses a Windows-safe image path.
  - Only the Desktop Video driver is a system install; everything else is bundled.
EOF

# marks a portable copy: its updater replaces the files in place from the
# portable zip (the installer deletes this file; an installed copy updates
# with the setup)
cat > "$PKG/portable.txt" << EOF
vrecord ${VERSION} portable package.
Updates (Config > Updates) unpack the new portable zip over this folder.
Delete this file only if this folder was installed with the vrecord setup.
EOF

(
  cd "$ARTIFACTS"
  if command -v zip >/dev/null 2>&1; then
    rm -f "vrecord_${VERSION}_windows_portable.zip"
    (cd "vrecord_windows_${VERSION}" && zip -qry "../vrecord_${VERSION}_windows_portable.zip" .)
  else
    powershell.exe -NoProfile -Command "Compress-Archive -Path 'vrecord_windows_${VERSION}\\*' -DestinationPath 'vrecord_${VERSION}_windows_portable.zip' -Force"
  fi
)

echo "Built: $PKG"
echo "Zip:   $ARTIFACTS/vrecord_${VERSION}_windows_portable.zip"
