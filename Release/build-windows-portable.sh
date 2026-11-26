#!/usr/bin/env bash
# Assemble a self-contained Windows portable package for vrecord.
# Expects prior builds under VRECORD_BUILD (default: $HOME/vrecord-build):
#   ffmpeg-install/, gtkdialog-install/, xmlstarlet-install/,
#   decklink-include/, mediainfo-cli/, mkvtoolnix/, dejavu fonts, cowsay/
#
# Output: Release/artifacts/vrecord_windows_<VERSION>/ (combined, development)
#   plus the release packages vrecord-qt6_windows_<VERSION>/ and
#   vrecord-gtk_windows_<VERSION>/, each with its portable .zip

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


# ---------------------------------------------------------------- flavours
# $PKG above is the combined development folder (both interfaces; what
# agent-build and the full test suite use). The release ships two packages
# made from it:
#   vrecord-qt6_windows_<v>/  Qt6 GUI as Vrecord.exe + the capture backend
#   vrecord-gtk_windows_<v>/  GTK interface (Vrecord.exe launcher + gtkdialog)
# A DLL leaves a package only when the removed programs needed it and
# nothing that stays does (ldd), so shared tools never lose a library.
QT_PKG="$ARTIFACTS/vrecord-qt6_windows_${VERSION}"
GTK_PKG="$ARTIFACTS/vrecord-gtk_windows_${VERSION}"

# every DLL inside <pkg> that the given binaries load (resolved paths, lower case)
package_deps() {
  local pkg_lc="${1,,}" ; shift
  local bin
  for bin in "$@" ; do
    [[ -f "$bin" ]] || continue
    { ldd "$bin" 2>/dev/null || true ; } | awk '{ print $3 }'
  done | tr 'A-Z' 'a-z' | { grep -F "$pkg_lc/" || true ; } | sort -u
}
# make_flavour <dest> <path relative to the package to remove>...
make_flavour() {
  local dest="$1" ; shift
  rm -rf "$dest"
  mkdir -p "$dest"
  # the script FIRST: MSYS matches "vrecord" to an existing Vrecord.exe
  # (.exe suffix magic), so copying it after the exe fails ("File exists")
  cp -a "$PKG/vrecord" "$dest/"
  local entry
  for entry in "$PKG"/* ; do
    if [[ "$(basename "$entry")" == "vrecord" ]] ; then continue ; fi
    cp -a "$entry" "$dest/"
  done
  # development leftovers never ship
  rm -f "$dest"/VrecordQt6-*.log "$dest"/VrecordQt6-*.txt "$dest"/VrecordQt6-*.jpg \
        "$dest"/VrecordQt6-*.conf "$dest"/VrecordQt6-*.conf.bak "$dest"/VrecordQt6-*.pid \
        "$dest"/vrecord-qt6-update.ini
  local removed=() rel
  for rel in "$@" ; do
    [[ -e "$dest/$rel" ]] || continue
    if [[ -d "$dest/$rel" ]] ; then
      while IFS= read -r f ; do removed+=("$f") ; done < <(find "$dest/$rel" -type f \( -iname '*.exe' -o -iname '*.dll' \))
    else
      removed+=("$dest/$rel")
    fi
  done
  local kept=()
  while IFS= read -r f ; do
    local is_removed=0 r
    for r in "${removed[@]}" ; do
      if [[ "$r" == "$f" ]] ; then is_removed=1 ; break ; fi
    done
    if [[ $is_removed = 0 ]] ; then kept+=("$f") ; fi
  done < <(find "$dest" -type f \( -iname '*.exe' -o -iname '*.dll' \))
  local removed_deps kept_deps
  removed_deps="$(package_deps "$dest" "${removed[@]}")"
  # what stays needs: every kept program, and every kept DLL that is NOT
  # one of the removed programs' libraries (plugins and libraries loaded
  # at run time keep themselves). The removed programs' libraries do not
  # count as users - libgdk needing libpango must not keep either.
  local roots=() lower
  for f in "${kept[@]}" ; do
    lower="${f,,}"
    if [[ "$lower" == *.exe ]] || ! grep -qxF "$lower" <<< "$removed_deps" ; then
      roots+=("$f")
    fi
  done
  kept_deps="$(package_deps "$dest" "${roots[@]}")"
  for rel in "$@" ; do rm -rf "${dest:?}/$rel" ; done
  local dll dropped=0
  while IFS= read -r dll ; do
    [[ -n "$dll" ]] || continue
    if grep -qxF "$dll" <<< "$kept_deps" ; then continue ; fi
    # resolve the lower-cased path back to the real file
    local real
    real="$(find "$dest" -type f -iname "$(basename "$dll")" | awk -v want="$dll" 'tolower($0) == want' | head -n 1)"
    if [[ -n "$real" ]] ; then rm -f "$real" ; dropped=$((dropped + 1)) ; fi
  done <<< "$removed_deps"
  echo "  $(basename "$dest"): removed $* and ${dropped} DLL(s) only they used"
}

echo "Making the Qt6 and GTK packages"
# Qt6: the GTK launcher and gtkdialog go; the Qt6 app becomes Vrecord.exe
make_flavour "$QT_PKG" Vrecord.exe bin/gtkdialog.exe vrecord.cmd vrecord.sh
mv "$QT_PKG/VrecordQt6.exe" "$QT_PKG/Vrecord.exe"
# GTK: the Qt programs, the Qt libraries and the Qt plugin folders go
QT_PARTS=(VrecordQt6.exe vsignal.exe VrecordOutputTest.exe)
for f in "$PKG"/Qt6*.dll ; do
  if [[ -f "$f" ]] ; then QT_PARTS+=("$(basename "$f")") ; fi
done
for d in "$PKG"/*/ ; do
  d="$(basename "$d")"
  case "$d" in bin|Resources|share) ;; *) QT_PARTS+=("$d") ;; esac
done
make_flavour "$GTK_PKG" "${QT_PARTS[@]}"

write_readme() {   # write_readme <package> <qt6|gtk>
  local pkg="$1" flavour="$2"
  {
    if [[ "$flavour" = qt6 ]] ; then
      echo "Vrecord ${VERSION} for Windows"
    else
      echo "Vrecord ${VERSION} for Windows (GTK interface)"
    fi
    echo "======================================="
    echo
    echo "Requirements:"
    echo "  - Blackmagic Desktop Video drivers"
    echo "  - Git for Windows or MSYS2 (C:\\msys64): the bash that runs the capture script"
    echo
    echo "Run:"
    echo "  Vrecord.exe"
    if [[ "$flavour" = qt6 ]] ; then
      echo "  vsignal start --pattern \"SMPTE SD Bars (75%)\" --connector HDMI"
      echo "  vsignal stop"
      echo
      echo "Install or portable:"
      echo "  - vrecord-qt6_<version>_windows_x86_64_setup.exe installs for the current"
      echo "    user (no administrator rights) into %LOCALAPPDATA%\\Programs\\vrecord,"
      echo "    with Start Menu shortcuts and an Apps & features entry."
      echo "  - vrecord-qt6_<version>_windows_x86_64_portable.zip runs from any folder"
      echo "    (portable.txt marks it as portable)."
      echo
      echo "Updates (Config > Updates):"
      echo "  - Off / tell me when an update is available (default) / download"
      echo "    automatically and install when Vrecord closes."
      echo "  - Only published Qt6 releases are offered. Every download is checked"
      echo "    against the release's SHA256SUMS.txt and refused when it does not match."
      echo "  - Installed copies run the new setup silently; portable copies unpack the"
      echo "    new zip over this folder (your logs stay). Nothing is installed while a"
      echo "    recording is running."
      echo "  - The install helper's log: %TEMP%\\vrecord-update\\<tag>\\apply-update.log"
    else
      echo "  vrecord.cmd           (the same, from a console)"
      echo "  vrecord.cmd -e        (edit settings)"
      echo "  vrecord.cmd mytape    (record without the window)"
      echo
      echo "The GTK package is portable only: unzip it anywhere. New versions are"
      echo "downloaded by hand from the GTK release on GitHub."
    fi
    echo
    echo "Notes:"
    echo "  - Output directories must be normal Windows paths (e.g. C:/Captures)."
    echo "  - ffmpeg-ma is a MinGW build with the DeckLink input/output and drawtext."
    echo "  - Only the Desktop Video driver (and a bash) are system installs;"
    echo "    everything else is bundled."
  } > "$pkg/README-WINDOWS.txt"
  # marks a portable copy (the Qt6 installer deletes it: an installed copy
  # updates with the setup)
  {
    echo "Vrecord ${VERSION} portable package."
    if [[ "$flavour" = qt6 ]] ; then echo "Updates (Config > Updates) unpack the new portable zip over this folder." ; fi
    echo "Delete this file only if this folder was installed with the Vrecord setup."
  } > "$pkg/portable.txt"
}
write_readme "$QT_PKG" qt6
write_readme "$GTK_PKG" gtk

zip_package() {   # zip_package <package dir> <zip name>
  (
    cd "$ARTIFACTS"
    rm -f "$2"
    if command -v zip >/dev/null 2>&1; then
      (cd "$(basename "$1")" && zip -qry "../$2" .)
    else
      powershell.exe -NoProfile -Command "Compress-Archive -Path '$(basename "$1")\\*' -DestinationPath '$2' -Force"
    fi
  )
}
QT_ZIP="vrecord-qt6_${VERSION}_windows_x86_64_portable.zip"
GTK_ZIP="vrecord-gtk_${VERSION}_windows_x86_64_portable.zip"
zip_package "$QT_PKG" "$QT_ZIP"
zip_package "$GTK_PKG" "$GTK_ZIP"

echo "Built: $PKG (combined, development)"
echo "Qt6:   $QT_PKG + $ARTIFACTS/$QT_ZIP"
echo "GTK:   $GTK_PKG + $ARTIFACTS/$GTK_ZIP"
