#!/bin/sh
# Builds a vrecord .AppImage.
# Uses linuxdeploy to package the AppDir (scripts + Resources + brew
# binaries + their linuxbrew shared libraries) into a portable
# self-contained .AppImage.

set -ex
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ARTIFACTS="$RELEASE_DIR/artifacts"
mkdir -p "$ARTIFACTS"

VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' ../vrecord)"
BREW_PREFIX="$(brew --prefix)"
APPDIR="$RELEASE_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/vrecord/bin" \
         "$APPDIR/usr/lib/vrecord/lib" \
         "$APPDIR/usr/share/vrecord/Resources" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps"

# --- vrecord scripts and resources ---
cp ../vrecord "$APPDIR/usr/bin/"
cp ../vtest "$APPDIR/usr/bin/" 2>/dev/null || true
cp ../vplay "$APPDIR/usr/bin/" 2>/dev/null || true
chmod 755 "$APPDIR/usr/bin/"*
cp -r ../Resources/* "$APPDIR/usr/share/vrecord/Resources/"

# --- runtime binaries ---
for BIN in ffmpeg-ma ffplay-ma ffprobe-ma gtkdialog mpv mediaconch mediainfo \
           mkvextract mkvinfo mkvmerge mkvpropedit xmlstarlet cowsay gnuplot \
           dvrescue dvpackager deckcontrol timecodexml2webvtt; do
    BINPATH="$(command -v "$BIN" 2>/dev/null || true)"
    if [ -n "$BINPATH" ]; then
        cp "$BINPATH" "$APPDIR/usr/lib/vrecord/bin/"
    fi
done

# --- linuxbrew shared libraries (resolved transitively: ldd the
#     executables AND every collected .so until no new ones appear,
#     otherwise deps-of-deps like libx264 under libavcodec go missing) ---
CHANGED=1
while [ "$CHANGED" = 1 ]; do
    CHANGED=0
    for BIN in "$APPDIR/usr/lib/vrecord/bin/"* "$APPDIR/usr/lib/vrecord/lib/"*; do
        [ -f "$BIN" ] || continue
        for LIB in $(ldd "$BIN" 2>/dev/null | awk '/=> \// { print $3 }'); do
            case "$LIB" in
                *linuxbrew*|*libavc1394*|*libraw1394*|*libiec61883*|*librom1394*)
                    # brew libs are only on the build machine, and the dv/1394
                    # libs are runtime deps of the bundled dvrescue
                    DEST="$APPDIR/usr/lib/vrecord/lib/$(basename "$LIB")"
                    if [ ! -f "$DEST" ]; then
                        cp "$LIB" "$DEST"
                        CHANGED=1
                    fi
                    ;;
            esac
        done
    done
done

# --- desktop entry and icon ---
cp ../Resources/vrecord-icon.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/vrecord.svg"
# linuxdeploy prefers a raster icon; convert the SVG when rsvg-convert is
# available (installed as a build dep), fall back to the SVG otherwise
ICON_FILE="$APPDIR/usr/share/icons/hicolor/scalable/apps/vrecord.svg"
if command -v rsvg-convert >/dev/null 2>&1; then
    mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
    if rsvg-convert -w 256 -h 256 \
        -o "$APPDIR/usr/share/icons/hicolor/256x256/apps/vrecord.png" \
        ../Resources/vrecord-icon.svg; then
        ICON_FILE="$APPDIR/usr/share/icons/hicolor/256x256/apps/vrecord.png"
    fi
fi
cat > "$APPDIR/usr/share/applications/vrecord.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=vrecord
GenericName=Video Capture
Comment=Capture video signals to digital files
Exec=vrecord
Icon=vrecord
Terminal=true
Categories=AudioVideo;Video;Recorder;
StartupWMClass=Gtkdialog
DESKTOP

# --- AppRun: launcher with bundled paths ---
cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/sh
# vrecord AppImage launcher
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/lib/vrecord/bin:$PATH"
export LD_LIBRARY_PATH="$HERE/usr/lib/vrecord/lib:${LD_LIBRARY_PATH}"
# bundled Resources location (the vrecord script honors VRECORD_RESOURCES)
export VRECORD_RESOURCES="$HERE/usr/share/vrecord/Resources"
exec "$HERE/usr/bin/vrecord" "$@"
APPRUN
chmod 755 "$APPDIR/AppRun"

# --- linuxdeploy ---
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage"
curl -fL "$LINUXDEPLOY_URL" -o /tmp/linuxdeploy
chmod +x /tmp/linuxdeploy
export OUTPUT="$ARTIFACTS/vrecord_${VERSION}_x86_64.AppImage"
export ARCH=x86_64
# CI runners have no FUSE: run linuxdeploy (and the appimagetool it calls)
# in extract-and-run mode instead of mounting AppImages
export APPIMAGE_EXTRACT_AND_RUN=1
/tmp/linuxdeploy --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/vrecord.desktop" \
    --icon-file "$ICON_FILE" \
    --output appimage

echo "Built: $OUTPUT"
