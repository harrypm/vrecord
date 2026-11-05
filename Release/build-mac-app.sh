#!/bin/sh
# Builds a standalone vrecord.app bundle for macOS.
# Runs the existing Release_Mac.sh to fetch/compile dependencies, then
# packages the result as a proper .app. On failure of the full release
# script, falls back to a minimal bundle using brew-installed deps.

set -x
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ARTIFACTS="$RELEASE_DIR/artifacts"
rm -rf "$ARTIFACTS"
mkdir -p "$ARTIFACTS"

APP="$ARTIFACTS/vrecord.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
mkdir -p "$MACOS" "$RESOURCES/bin" "$RESOURCES/share/vrecord/Resources"

VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' ../vrecord)"

# --- Attempt the full release build (fetches/compiles all deps) ---
if ./Release_Mac.sh 2>&1; then
    echo "full release build OK"
    # copy the built files from vrecord_ROOT
    ROOT="$RELEASE_DIR/vrecord_ROOT/usr/local"
    cp -a "$ROOT/bin/"* "$MACOS/" 2>/dev/null || true
    cp -a "$ROOT/share/vrecord/Resources/"* "$RESOURCES/share/vrecord/Resources/" 2>/dev/null || true
    # bundled libs
    cp -a "$ROOT/lib/vrecord/lib/"* "$CONTENTS/Frameworks/" 2>/dev/null || mkdir -p "$CONTENTS/Frameworks" && cp -a "$ROOT/lib/vrecord/lib/"* "$CONTENTS/Frameworks/" 2>/dev/null || true
else
    echo "::warning::Release_Mac.sh failed - building minimal .app from brew deps"
    # fallback: brew-installed vrecord + deps
    if ! command -v vrecord >/dev/null 2>&1; then
        brew tap amiaopensource/amiaos
        brew tap mediaarea/mediaarea
        brew install vrecord ffmpeg-ma gtkdialog mpv mediaconch mkvtoolnix gnuplot xmlstarlet
    fi
    BREW_PREFIX="$(brew --prefix)"
    cp "$(command -v vrecord)" "$MACOS/"
    cp "$(command -v vtest)" "$MACOS/" 2>/dev/null || true
    cp "$(command -v vplay)" "$MACOS/" 2>/dev/null || true
    cp -a "$BREW_PREFIX/share/vrecord/Resources/"* "$RESOURCES/share/vrecord/Resources/" 2>/dev/null || true
    # runtime binaries from brew
    for BIN in ffmpeg-ma ffplay-ma ffprobe-ma gtkdialog mpv mediaconch mediainfo mkvextract mkvinfo mkvmerge mkvpropedit xmlstarlet cowsay gnuplot dvrescue; do
        BINPATH="$(command -v "$BIN" 2>/dev/null)"
        [ -n "$BINPATH" ] && cp "$BINPATH" "$RESOURCES/bin/"
    done
    cat > "$MACOS/vrecord" << 'LAUNCHER'
#!/bin/sh
# vrecord launcher: runs the bundled script with the bundled deps
HERE="$(cd "$(dirname "$0")" && pwd)"
export PATH="$HERE/../Resources/bin:$PATH"
exec /bin/sh "$HERE/vrecord-real" "$@"
LAUNCHER
    mv "$MACOS/vrecord" "$MACOS/vrecord-real" 2>/dev/null || true
    chmod +x "$MACOS/vrecord" "$MACOS/vrecord-real"
fi

# --- Info.plist ---
cat > "$CONTENTS/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>vrecord</string>
    <key>CFBundleDisplayName</key>
    <string>vrecord</string>
    <key>CFBundleIdentifier</key>
    <string>com.github.amiaopensource.vrecord</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>vrecord</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSCameraUsageDescription</key>
    <string>vrecord needs camera access to capture video from DeckLink devices.</string>
</dict>
</plist>
EOF

# --- icon (if available) ---
if [ -f "$RESOURCES/share/vrecord/Resources/vrecord-logo.icns" ]; then
    cp "$RESOURCES/share/vrecord/Resources/vrecord-logo.icns" "$RESOURCES/AppIcon.icns"
fi

echo "APPL????" > "$CONTENTS/PkgInfo"

# --- zip for upload ---
cd "$ARTIFACTS"
zip -qry vrecord.app.zip vrecord.app
echo "Built: $ARTIFACTS/vrecord.app and vrecord.app.zip"
