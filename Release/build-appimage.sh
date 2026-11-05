#!/bin/sh
# Builds a vrecord .AppImage.
# Uses linuxdeploy to package the AppDir (scripts + Resources + brew
# binaries + their linuxbrew shared libraries) into a portable
# self-contained .AppImage.

set -x
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
cp ../Resources/* "$APPDIR/usr/share/vrecord/Resources/"

# --- runtime binaries ---
for BIN in ffmpeg-ma ffplay-ma ffprobe-ma gtkdialog mpv mediaconch mediainfo \
           mkvextract mkvinfo mkvmerge mkvpropedit xmlstarlet cowsay gnuplot \
           dvrescue dvpackager; do
    BINPATH="$(command -v "$BIN" 2>/dev/null)"
    [ -n "$BINPATH" ] && cp "$BINPATH" "$APPDIR/usr/lib/vrecord/bin/"
done

# --- linuxbrew shared libraries ---
for BIN in "$APPDIR/usr/lib/vrecord/bin/"*; do
    [ -f "$BIN" ] || continue
    ldd "$BIN" 2>/dev/null | awk '/=> \// { print $3 }' | while read -r LIB; do
        case "$LIB" in
            *linuxbrew*)
                DEST="$APPDIR/usr/lib/vrecord/lib/$(basename "$LIB")"
                [ -f "$DEST" ] || cp "$LIB" "$DEST"
                ;;
        esac
done
done

# --- desktop entry and icon ---
cp ../Resources/vrecord-icon.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/vrecord.svg"
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
# AppDir Resources path (the vrecord script resolves via SCRIPTDIR)
export VRECORD_RESOURCES="$HERE/usr/share/vrecord/Resources"
exec "$HERE/usr/bin/vrecord" "$@"
APPRUN
chmod 755 "$APPDIR/AppRun"

# --- linuxdeploy ---
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage"
curl -L "$LINUXDEPLOY_URL" -o /tmp/linuxdeploy
chmod +x /tmp/linuxdeploy
export OUTPUT="$ARTIFACTS/vrecord_${VERSION}_x86_64.AppImage"
export ARCH=x86_64
/tmp/linuxdeploy --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/vrecord.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/vrecord.svg" \
    --output appimage

echo "Built: $OUTPUT"
