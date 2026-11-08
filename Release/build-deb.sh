#!/bin/sh
# Builds a vrecord .deb package.
# Packages the vrecord scripts + Resources + brew-installed runtime
# dependencies (ffmpeg-ma, gtkdialog, mediaconch, etc.) into a .deb
# that installs to /usr/local/{bin,lib/vrecord,share/vrecord}.

set -ex
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ARTIFACTS="$RELEASE_DIR/artifacts"
rm -rf "$ARTIFACTS"
mkdir -p "$ARTIFACTS"

VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' ../vrecord)"
BREW_PREFIX="$(brew --prefix)"
PKGROOT="$RELEASE_DIR/deb-pkg"
rm -rf "$PKGROOT"

PREFIX="$PKGROOT/usr/local"
mkdir -p "$PREFIX/bin" "$PREFIX/lib/vrecord/bin" "$PREFIX/lib/vrecord/lib" \
         "$PREFIX/share/vrecord/Resources" "$PREFIX/share/applications" \
         "$PREFIX/share/icons/hicolor/scalable/apps" \
         "$PKGROOT/DEBIAN"

# --- vrecord scripts and resources ---
cp ../vrecord "$PREFIX/bin/"
cp ../vtest "$PREFIX/bin/" 2>/dev/null || true
cp ../vplay "$PREFIX/bin/" 2>/dev/null || true
cp -r ../Resources/* "$PREFIX/share/vrecord/Resources/"
mkdir -p "$PREFIX/share/man/man1"
cp ../vrecord.1 "$PREFIX/share/man/man1/"
cp ../vtest.1 "$PREFIX/share/man/man1/"

# --- runtime binaries from brew (linuxbrew builds are portable enough
#     when glibc matches; we target ubuntu 24.04 glibc 2.39) ---
for BIN in ffmpeg-ma ffplay-ma ffprobe-ma gtkdialog mpv mediaconch mediainfo \
           mkvextract mkvinfo mkvmerge mkvpropedit xmlstarlet cowsay gnuplot \
           dvrescue dvpackager deckcontrol timecodexml; do
    BINPATH="$(command -v "$BIN" 2>/dev/null || true)"
    if [ -n "$BINPATH" ]; then
        cp "$BINPATH" "$PREFIX/lib/vrecord/bin/"
    fi
done

# --- shared libraries needed by the brew binaries (resolved
#     transitively: ldd the executables AND every collected .so until
#     no new ones appear, otherwise deps-of-deps like libx264 under
#     libavcodec go missing) ---
CHANGED=1
while [ "$CHANGED" = 1 ]; do
    CHANGED=0
    for BIN in "$PREFIX/lib/vrecord/bin/"* "$PREFIX/lib/vrecord/lib/"*; do
        [ -f "$BIN" ] || continue
        for LIB in $(ldd "$BIN" 2>/dev/null | awk '/=> \// { print $3 }'); do
            case "$LIB" in
                *linuxbrew*|*opt/homebrew*|*libavc1394*|*libraw1394*|*libiec61883*|*librom1394*)
                    # brew libs are only on the build machine, and the dv/1394
                    # libs are runtime deps of the bundled dvrescue that the
                    # target system may not have
                    DEST="$PREFIX/lib/vrecord/lib/$(basename "$LIB")"
                    if [ ! -f "$DEST" ]; then
                        cp "$LIB" "$DEST"
                        CHANGED=1
                    fi
                    ;;
            esac
        done
    done
done

# --- desktop entry ---
cp ../Resources/vrecord-icon.svg "$PREFIX/share/icons/hicolor/scalable/apps/vrecord.svg"
cat > "$PREFIX/share/applications/vrecord.desktop" << 'DESKTOP'
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

# --- postinst: path setup + font cache ---
cat > "$PKGROOT/DEBIAN/postinst" << 'POSTINST'
#!/bin/sh
# add vrecord deps to PATH + bundled libs to LD_LIBRARY_PATH via profile.d
if [ ! -f /etc/profile.d/vrecord.sh ]; then
    cat > /etc/profile.d/vrecord.sh << 'EOF'
export PATH="/usr/local/lib/vrecord/bin:$PATH"
export LD_LIBRARY_PATH="/usr/local/lib/vrecord/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
EOF
fi
echo "vrecord installed. Relogin or run: source /etc/profile.d/vrecord.sh"
POSTINST
chmod 755 "$PKGROOT/DEBIAN/postinst"

# --- control file (note: no comments allowed inside, dpkg rejects them) ---
ARCH="$(dpkg --print-architecture)"
INSTALLED_SIZE="$(du -sk "$PKGROOT" | awk '{print $1}')"
cat > "$PKGROOT/DEBIAN/control" << EOF
Package: vrecord
Version: ${VERSION}
Section: video
Priority: optional
Architecture: ${ARCH}
Depends: libc6 (>= 2.39)
Maintainer: harrypm <harry@harrymunday.com>
Installed-Size: ${INSTALLED_SIZE}
Description: Open-source video capture tool for archivists
 Vrecord captures analog and digital video signals through Blackmagic
 Design capture cards (DeckLink) via FFmpeg, producing FFV1/Matroska
 preservation files with QCTools reports, frame MD5s, and MediaConch
 validation. Also supports DV capture via DVRescue.
 .
 Includes bundled ffmpeg-ma (MediaArea FFmpeg with DeckLink support)
 and gtkdialog for the settings GUI.
Homepage: https://github.com/amiaopensource/vrecord
EOF

# --- build ---
chmod 755 "$PREFIX/bin/"*
dpkg-deb --build "$PKGROOT" "$ARTIFACTS/vrecord_${VERSION}_${ARCH}.deb"
rm -rf "$PKGROOT"
echo "Built: $ARTIFACTS/vrecord_${VERSION}_${ARCH}.deb"
