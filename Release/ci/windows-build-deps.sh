#!/usr/bin/env bash
# Recreate the Windows build inputs (VRECORD_BUILD) from pinned upstream
# sources + the vendored vrecord patches, for Release/build-windows-portable.sh.
# Run in an MSYS2 UCRT64 shell (CI: msys2/setup-msys2 with the packages listed
# in .github/workflows/build-windows.yml).
#
#   VRECORD_BUILD=/d/vrecord-build bash Release/ci/windows-build-deps.sh
#
# Every download is pinned by sha256. Each step is skipped when its output
# already exists (the CI cache keeps them between runs).
set -euo pipefail
export TMPDIR=/tmp TMP=/tmp TEMP=/tmp
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
B="${VRECORD_BUILD:-$HOME/vrecord-build}"
DL="$B/downloads"
mkdir -p "$B" "$DL"
JOBS="$(nproc)"

fetch() {   # fetch <url> <file> <sha256>
    local url="$1" file="$DL/$2" sum="$3"
    if [[ ! -f "$file" ]] || ! echo "$sum  $file" | sha256sum -c --status ; then
        echo "::group::download $2"
        curl -fL --retry 3 --retry-delay 5 "$url" -o "$file.part"
        mv -f "$file.part" "$file"
        echo "::endgroup::"
    fi
    if ! echo "$sum  $file" | sha256sum -c --status ; then
        echo "::error::checksum mismatch for $2 ($(sha256sum "$file" | cut -c1-64))"
        exit 1
    fi
}

step() { echo "=== $* ===" ; }

# ---------------------------------------------------------------- DeckLink headers
# MartinPulec/desktopvideo_sdk-api v16.0 (BSL-1.0): its CI builds the
# C-compatible Windows API with widl (deck-win-api.zip) + vrecord's shim for
# FFmpeg 9's DeckLinkAPI_v14_2_1.h include
if [[ ! -f "$B/decklink-include/DeckLinkAPI.h" ]] ; then
    step "DeckLink SDK headers"
    fetch "https://github.com/MartinPulec/desktopvideo_sdk-api/releases/download/v16.0/deck-win-api.zip" \
          deck-win-api.zip 01ed12bdbb8717b62bf9d6b6186a63cfc71d5e30249fcb8ba919dfe8364d6fd0
    rm -rf "$B/decklink-include" && mkdir -p "$B/decklink-include"
    unzip -o -q "$DL/deck-win-api.zip" -d "$B/decklink-include"
    cp -f "$ROOT/Release/vendor/decklink-win/DeckLinkAPI_v14_2_1.h" "$B/decklink-include/"
fi

# ---------------------------------------------------------------- ffmpeg-ma
# FFmpeg 9.0.1 + MediaArea's ffmpeg-ma patch set (pinned commit, as the
# mediaarea/homebrew-mediaarea formula) + vrecord's Windows DeckLink patch,
# built with decklink + drawtext (Release/build-ffmpeg-windows.sh flags)
if [[ ! -x "$B/ffmpeg-install/bin/ffmpeg.exe" ]] ; then
    step "ffmpeg-ma"
    fetch "https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz" ffmpeg-9.0.1.tar.xz \
          cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635
    rm -rf "$B/ffmpeg-9.0.1"
    tar -xJf "$DL/ffmpeg-9.0.1.tar.xz" -C "$B"
    PATCH_COMMIT="$(cat "$ROOT/Release/vendor/ffmpeg-ma/PATCH_COMMIT")"
    while read -r name sum ; do
        [[ -z "$name" || "$name" == \#* ]] && continue
        fetch "https://raw.githubusercontent.com/MediaArea/ffmpeg-ma-patch/${PATCH_COMMIT}/${name}" "$name" "$sum"
        (cd "$B/ffmpeg-9.0.1" && patch -p1 -N -s -i "$DL/$name")
    done < "$ROOT/Release/vendor/ffmpeg-ma/series"
    for p in "$ROOT"/Release/vendor/ffmpeg-ma/0100-*.patch ; do
        (cd "$B/ffmpeg-9.0.1" && patch -p1 -N -s -i "$p")
    done
    bash "$ROOT/Release/build-ffmpeg-windows.sh" "$B"
fi

# ---------------------------------------------------------------- deckcontrol
# bavc/deckcontrol v0.6 (unmodified) + vrecord's COM dispatch for Windows
if [[ ! -x "$B/deckcontrol-src/deckcontrol.exe" ]] ; then
    step "deckcontrol"
    fetch "https://github.com/bavc/deckcontrol/archive/refs/tags/v0.6.tar.gz" deckcontrol-0.6.tar.gz \
          e4a184ff6ec64aef8cccd15712797e7507d2de1ce873396703d575047ae1463e
    rm -rf "$B/deckcontrol-src" "$B/deckcontrol-0.6"
    tar -xzf "$DL/deckcontrol-0.6.tar.gz" -C "$B"
    mv "$B/deckcontrol-0.6" "$B/deckcontrol-src"
    cp -f "$ROOT"/Release/vendor/decklink-win/DeckLinkAPIDispatch_win.* "$B/deckcontrol-src/"
    (cd "$B/deckcontrol-src" && g++ -O2 -I"$B/decklink-include" -include DeckLinkAPIDispatch_win.h \
        deckcontrol.cpp DeckLinkAPIDispatch_win.cpp -o deckcontrol.exe -pthread -lole32 -loleaut32)
fi

# ---------------------------------------------------------------- gtkdialog (legacy UI)
# puppylinux-woof-CE gtkdialog 0.8.5e (as the amiaos formula) + vrecord's
# Windows patch; GTK3 build
if [[ ! -x "$B/gtkdialog-install/bin/gtkdialog.exe" ]] ; then
    step "gtkdialog"
    fetch "https://github.com/puppylinux-woof-CE/gtkdialog/archive/refs/tags/0.8.5e.tar.gz" gtkdialog-0.8.5e.tar.gz \
          93561ed4042c113b85aa5550f51ae4e6980a5de6e083db5c358d6fc2fb2feb0a
    rm -rf "$B/gtkdialog-0.8.5e"
    tar -xzf "$DL/gtkdialog-0.8.5e.tar.gz" -C "$B"
    cd "$B/gtkdialog-0.8.5e"
    patch -p1 -N -s -i "$ROOT/Release/vendor/gtkdialog/0001-vrecord-windows.patch"
    ./autogen.sh > /dev/null
    ./configure --prefix="$B/gtkdialog-install" --enable-gtk3 > /dev/null
    make -j"$JOBS" > /dev/null
    make install > /dev/null
    cd "$ROOT"
fi

# ---------------------------------------------------------------- xmlstarlet
if [[ ! -x "$B/xmlstarlet-install/bin/xml.exe" && ! -x "$B/xmlstarlet-install/bin/xmlstarlet.exe" ]] ; then
    step "xmlstarlet"
    fetch "https://downloads.sourceforge.net/project/xmlstar/xmlstarlet/1.6.1/xmlstarlet-1.6.1.tar.gz" xmlstarlet-1.6.1.tar.gz \
          15d838c4f3375332fd95554619179b69e4ec91418a3a5296e7c631b7ed19e7ca
    rm -rf "$B/xmlstarlet-1.6.1"
    tar -xzf "$DL/xmlstarlet-1.6.1.tar.gz" -C "$B"
    cd "$B/xmlstarlet-1.6.1"
    for p in "$ROOT"/Release/patches/xmlstarlet/*.patch ; do
        patch -p1 -N -s -i "$p"
    done
    ./configure --prefix="$B/xmlstarlet-install" > /dev/null
    make -j"$JOBS" > /dev/null
    make install > /dev/null
    cd "$ROOT"
fi

# ---------------------------------------------------------------- prebuilt CLI tools
if [[ ! -f "$B/mediainfo-cli/MediaInfo.exe" ]] ; then
    step "MediaInfo CLI"
    fetch "https://mediaarea.net/download/binary/mediainfo/26.05/MediaInfo_CLI_26.05_Windows_x64.zip" mediainfo-cli.zip \
          f7f80620ce6d14f4995f0de6f98e3ef18ad29496db01899571152ee3311229f9
    rm -rf "$B/mediainfo-cli" && unzip -o -q "$DL/mediainfo-cli.zip" -d "$B/mediainfo-cli"
fi
if [[ ! -f "$B/mediaconch-x/MediaConch.exe" ]] ; then
    step "MediaConch CLI"
    fetch "https://mediaarea.net/download/binary/mediaconch/25.04/MediaConch_CLI_25.04_Windows_x64.zip" mediaconch-cli.zip \
          f5d7cb18829c3c6b5e6ec01c531cc7b2423ee3cd0795cf7113fce0aef67dc5b6
    rm -rf "$B/mediaconch-x" && unzip -o -q "$DL/mediaconch-cli.zip" -d "$B/mediaconch-x"
fi
if [[ ! -f "$B/dvrescue-x/DVRescue.exe" && ! -f "$B/dvrescue-x/dvrescue.exe" ]] ; then
    step "DVRescue CLI"
    fetch "https://mediaarea.net/download/binary/dvrescue/24.07/dvrescue_CLI_24.07_Windows_x64.zip" dvrescue-cli.zip \
          6ed5fa84d83732da3a167330b6f367e9f2231a46b8e14f68ff02a2415fb5bd77
    rm -rf "$B/dvrescue-x" && unzip -o -q "$DL/dvrescue-cli.zip" -d "$B/dvrescue-x"
    # the package expects DVRescue.exe (the name the shell resolves case-insensitively)
    [[ -f "$B/dvrescue-x/dvrescue.exe" && ! -f "$B/dvrescue-x/DVRescue.exe" ]] && mv "$B/dvrescue-x/dvrescue.exe" "$B/dvrescue-x/DVRescue.exe"
fi
if [[ ! -f "$B/mkvtoolnix/mkvtoolnix/mkvpropedit.exe" ]] ; then
    step "MKVToolNix"
    fetch "https://mkvtoolnix.download/windows/releases/103.0/mkvtoolnix-64-bit-103.0.7z" mkvtoolnix-64-bit-103.0.7z \
          01d76dbfdbd5615d9e02f69b0710d1429a902d47a98c8eee33bd884c4fcb3cbf
    rm -rf "$B/mkvtoolnix" && mkdir -p "$B/mkvtoolnix"
    7z x -y -o"$B/mkvtoolnix" "$DL/mkvtoolnix-64-bit-103.0.7z" > /dev/null
fi
if [[ ! -f "$B/dejavu-fonts/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf" ]] ; then
    step "DejaVu fonts"
    fetch "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip" dejavu-fonts-ttf-2.37.zip \
          7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a
    rm -rf "$B/dejavu-fonts" && unzip -o -q "$DL/dejavu-fonts-ttf-2.37.zip" -d "$B/dejavu-fonts"
fi
if [[ ! -f "$B/cowsay/bin/cowsay" ]] ; then
    step "cowsay"
    fetch "https://github.com/cowsay-org/cowsay/archive/refs/tags/v3.8.4.tar.gz" cowsay-3.8.4.tar.gz \
          c15bc10712835d3a9bcda780dc9453362567bf48d1185905dc7ef2334d79aadd
    rm -rf "$B/cowsay-3.8.4"
    tar -xzf "$DL/cowsay-3.8.4.tar.gz" -C "$B"
    make -C "$B/cowsay-3.8.4" install prefix="$B/cowsay" > /dev/null
fi
# mpv for the QC view: MSYS2's own build (its DLLs are collected by the
# package's UCRT64 dependency sweep)
if [[ ! -f "$B/mpv-x/mpv.exe" ]] ; then
    step "mpv (MSYS2)"
    mkdir -p "$B/mpv-x"
    cp -f /ucrt64/bin/mpv.exe "$B/mpv-x/"
    [[ -f /ucrt64/bin/mpv.com ]] && cp -f /ucrt64/bin/mpv.com "$B/mpv-x/"
    touch "$B/mpv-x/.msys2"
fi
step "build inputs ready in $B"
ls "$B"
