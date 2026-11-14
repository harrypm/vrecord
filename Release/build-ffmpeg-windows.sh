#!/usr/bin/env bash
# Build ffmpeg-ma for the Windows portable package (MSYS2 UCRT64 shell).
#
# Source: FFmpeg 9.0.1 with the vrecord patch set (ffmpeg-ma-patch/*.patch)
# applied, plus the DeckLink SDK headers in decklink-include. Run from an
# MSYS2 UCRT64 login shell (TMPDIR must be an MSYS path: Git Bash's Windows
# TMPDIR makes configure's sanity test fail):
#
#   MSYSTEM=UCRT64 bash -l Release/build-ffmpeg-windows.sh [BUILD_DIR]
#
# drawtext needs freetype + harfbuzz (FFmpeg 6.1+): without them every
# playback view that labels or numbers its scopes fails or loses its text.
# Needed packages: mingw-w64-ucrt-x86_64-{freetype,harfbuzz,fontconfig,
# fribidi,SDL2,x264,openh264}.
#
# The installed bin/ffmpeg.exe, ffplay.exe and ffprobe.exe are shipped renamed
# to ffmpeg-ma.exe / ffplay-ma.exe / ffprobe-ma.exe (build-windows-portable.sh
# copies the DLLs and collects their UCRT64 dependencies).
set -u
export PATH=/ucrt64/bin:/usr/bin:$PATH TMPDIR=/tmp TMP=/tmp TEMP=/tmp
B="${1:-/c/Users/Harry/vrecord-build}"
cd "$B/ffmpeg-9.0.1" || { echo "missing $B/ffmpeg-9.0.1" ; exit 1 ; }
# Opus recording: libopus when the UCRT64 opus package is installed
# (pacman -S mingw-w64-ucrt-x86_64-opus; CI installs it), otherwise vrecord
# falls back to ffmpeg's own experimental opus encoder
EXTRA_LIBS=()
if pkg-config --exists opus 2>/dev/null ; then
    EXTRA_LIBS+=(--enable-libopus)
else
    echo "note: opus (pkg-config) not found - building without libopus"
fi
./configure \
    --prefix="$B/ffmpeg-install" \
    --enable-shared \
    --disable-static \
    --enable-gpl \
    --enable-decklink \
    --extra-cflags="-I$(cygpath -m "$B/decklink-include")" \
    --extra-cxxflags="-I$(cygpath -m "$B/decklink-include")" \
    --enable-sdl2 \
    --enable-libx264 \
    --enable-libopenh264 \
    --enable-libfreetype \
    --enable-libharfbuzz \
    --enable-libfontconfig \
    --enable-libfribidi \
    "${EXTRA_LIBS[@]}" || exit 1
make -j"$(nproc)" || exit 1
make install || exit 1
# the package (and vrecord) run the -ma names
for prog in ffmpeg ffplay ffprobe ; do
    cp -f "$B/ffmpeg-install/bin/${prog}.exe" "$B/ffmpeg-install/bin/${prog}-ma.exe"
done
# read the whole list (an early-exiting grep -q can leave ffplay blocked)
FILTERS="$("$B/ffmpeg-install/bin/ffplay.exe" -hide_banner -filters 2>/dev/null)"
if [[ "${FILTERS}" == *" drawtext "* ]] ; then
    echo "ffmpeg-ma built with drawtext"
else
    echo "WARNING: built without drawtext" ; exit 1
fi
