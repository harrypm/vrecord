##  Copyright (c) MediaArea.net SARL. All Rights Reserved.
 #
 #  Use of this source code is governed by a BSD-style license that can
 #  be found in the License.html file in the root of the source tree.
 ##

#!/bin/bash

# This script uses the following environment variables:
# - ARCH: target architecture - x86_64, arm64, or universal (default: universal).
#   Everything built from source is compiled for the target architecture and
#   arch-specific prebuilts are selected per target (mpv). The few prebuilt
#   dependencies that have no arm64 build anywhere (gtkdialog, MKVToolNix,
#   dvrescue) are bundled as x86_64 in every package; on Apple Silicon they
#   run through Rosetta 2. ffmpeg-ma, MediaInfo, MediaConch, and gnuplot are
#   downloaded as universal binaries and work natively everywhere.
# - MACOS_CODESIGN_IDENTITY: The subject and ID part of the Apple development certificate (optional).
# - BMSDK: The path to the Blackmagic DeckLink SDK include directory (if not in the standard include path).

set -e

#-----------------------------------------------------------------------
# Setup
release_directory="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
version="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' "${release_directory}/../vrecord")"

export MAKEOPTS=-j$(($(sysctl -n hw.logicalcpu)+1))

# Target architecture: x86_64, arm64, or universal (both slices).
ARCH="${ARCH:-universal}"
case "${ARCH}" in
    x86_64|arm64|universal) ;;
    *) echo "ERROR: ARCH must be x86_64, arm64, or universal (got '${ARCH}')." >&2 ; exit 1 ;;
esac
if [ "${ARCH}" = "universal" ] ; then
    MACOS_ARCH_FLAGS="-arch x86_64 -arch arm64"
    ARCH_SUFFIX=""
else
    MACOS_ARCH_FLAGS="-arch ${ARCH}"
    ARCH_SUFFIX="_${ARCH}"
fi
export MACOS_ARCH_FLAGS

export CXXFLAGS="-mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS} $CXXFLAGS"
export CFLAGS="-mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS} $CFLAGS"
export LDFLAGS="-mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS} $LDFLAGS"

# When building for a foreign architecture (e.g. arm64 on an x86_64 host),
# tell autoconf we are cross compiling so configure skips run-time test
# programs instead of aborting with "cannot run C compiled programs".
# Building universal binaries on any host still runs (the host slice
# executes), so no --host flag is needed there.
HOST_ARCH="$(uname -m)"
CONFIGURE_HOST_FLAGS=""
if [ "${ARCH}" = "arm64" ] && [ "${HOST_ARCH}" != "arm64" ] ; then
    CONFIGURE_HOST_FLAGS="--host=aarch64-apple-darwin"
elif [ "${ARCH}" = "x86_64" ] && [ "${HOST_ARCH}" != "x86_64" ] ; then
    CONFIGURE_HOST_FLAGS="--host=x86_64-apple-darwin"
fi

#-----------------------------------------------------------------------
# Cleanup
rm -f "${release_directory}"/vrecord_*_Mac*.dmg
rm -f "${release_directory}/vrecord.unsigned.pkg"
rm -f "${release_directory}/vrecord.pkg"

rm -f "${release_directory}/FFmpeg_Bin_Latest_Mac_Static_universal.zip"

rm -fr "${release_directory}/mediainfo_ROOT"
rm -f "${release_directory}/MediaInfo.dmg"
rm -f "${release_directory}/mediainfo.pkg"

rm -fr "${release_directory}/mediaconch_ROOT"
rm -f "${release_directory}/MediaConch.dmg"
rm -f "${release_directory}/mediaconch.pkg"

rm -fr "${release_directory}/dvrescue_ROOT"
rm -f "${release_directory}/dvrescue.dmg"
rm -f "${release_directory}/dvrescue.pkg"

rm -fr "${release_directory}/libxml2"
rm -fr "${release_directory}/libxslt"
rm -fr "${release_directory}/xmlstarlet"

rm -fr "${release_directory}/gtkdialog"
rm -fr "${release_directory}/timecodexml"
rm -fr "${release_directory}/deckcontrol"
rm -fr "${release_directory}/decklink-sdk-headers"

rm -fr "${release_directory}/gnuplot_ROOT"
rm -f "${release_directory}/gnuplot-qt5-universal.pkg"

rm -fr "${release_directory}/vrecord_ROOT"

mkdir -p "${release_directory}"/vrecord_ROOT/usr/local/{bin,share/{vrecord/Resources,man/man1},lib/vrecord/{app,bin,lib}}

#-----------------------------------------------------------------------
# Get cowsay
pushd "${release_directory}/"
    mkdir -p vrecord_ROOT/usr/local/lib/vrecord/share/cowsay/cows
    curl -L "https://raw.githubusercontent.com/cowsay-org/cowsay/refs/heads/main/share/cowsay/cows/default.cow" -o vrecord_ROOT/usr/local/lib/vrecord/share/cowsay/cows/default.cow
    curl -L "https://raw.githubusercontent.com/cowsay-org/cowsay/refs/heads/main/bin/cowsay" -o vrecord_ROOT/usr/local/lib/vrecord/bin/cowsay
    chmod +x vrecord_ROOT/usr/local/lib/vrecord/bin/cowsay
popd

#-----------------------------------------------------------------------
# Get ffmpeg-ma
pushd "${release_directory}/"
    curl -LO "https://mediaarea.net/download/snapshots/binary/ffmpeg/latest/FFmpeg_Bin_Latest_Mac_Static_universal.zip"
    # bin/ffprobe is also bundled (as ffprobe-ma): vrecord requires it and a
    # non-brew install has no other source for it.
    unzip -d vrecord_ROOT/usr/local/lib/vrecord -x FFmpeg_Bin_Latest_Mac_Static_universal.zip bin/ffmpeg bin/ffplay bin/ffprobe
    mv vrecord_ROOT/usr/local/lib/vrecord/bin/ffmpeg vrecord_ROOT/usr/local/lib/vrecord/bin/ffmpeg-ma
    mv vrecord_ROOT/usr/local/lib/vrecord/bin/ffplay vrecord_ROOT/usr/local/lib/vrecord/bin/ffplay-ma
    mv vrecord_ROOT/usr/local/lib/vrecord/bin/ffprobe vrecord_ROOT/usr/local/lib/vrecord/bin/ffprobe-ma
popd


#-----------------------------------------------------------------------
# Get MediaInfo CLI
pushd "${release_directory}/"
    mi_version=$(curl -Ls https://mediaarea.net/download/binary/mediainfo | grep -Eo 'href="[0-9.]+/"' | head -n1 | grep -Eo '[0-9.]+')
    curl -L "https://mediaarea.net/download/binary/mediainfo/${mi_version}/MediaInfo_CLI_${mi_version}_Mac.dmg" -o MediaInfo.dmg

    hdiutil attach -noverify MediaInfo.dmg
    cp "/Volumes/MediaInfo/mediainfo.pkg" .
    hdiutil detach "/Volumes/MediaInfo"

    pkgutil --expand-full mediainfo.pkg mediainfo_ROOT
    cp -a mediainfo_ROOT/Payload/usr/local/bin/mediainfo vrecord_ROOT/usr/local/lib/vrecord/bin
popd

#-----------------------------------------------------------------------
# Get MediaConch CLI
pushd "${release_directory}/"
    mc_version=$(curl -Ls https://mediaarea.net/download/binary/mediaconch | grep -Eo 'href="[0-9.]+/"' | head -n1 | grep -Eo '[0-9.]+')
    curl -L "https://mediaarea.net/download/binary/mediaconch/${mc_version}/MediaConch_CLI_${mc_version}_Mac.dmg" -o MediaConch.dmg

    hdiutil attach -noverify MediaConch.dmg
    cp "/Volumes/MediaConch/mediaconch.pkg" .
    hdiutil detach "/Volumes/MediaConch"

    pkgutil --expand-full mediaconch.pkg mediaconch_ROOT
    cp -a mediaconch_ROOT/Payload/usr/local/bin/mediaconch vrecord_ROOT/usr/local/lib/vrecord/bin
popd

#-----------------------------------------------------------------------
# Get dvrescue CLI
# x86_64-only prebuilt (MediaArea ships no arm64 Mac CLI and upstream master
# does not currently compile from source); bundled in all packages and runs
# under Rosetta 2 on Apple Silicon.
pushd "${release_directory}/"
    dv_version=$(curl -Ls https://mediaarea.net/download/binary/dvrescue | grep -Eo 'href="[0-9.]+/"' | head -n1 | grep -Eo '[0-9.]+')
    curl -L "https://mediaarea.net/download/binary/dvrescue/${dv_version}/dvrescue_CLI_${dv_version}_Mac.dmg" -o dvrescue.dmg

    hdiutil attach -noverify dvrescue.dmg
    cp "/Volumes/dvrescue/dvrescue.pkg" .
    hdiutil detach "/Volumes/dvrescue"

    pkgutil --expand-full dvrescue.pkg dvrescue_ROOT
    cp -a dvrescue_ROOT/Payload/usr/local/bin/* vrecord_ROOT/usr/local/lib/vrecord/bin
popd

#-----------------------------------------------------------------------
# Get gnuplot
pushd "${release_directory}/"
    curl -L https://csml.northwestern.edu/Download/Gnuplot/gnuplot-6.0.3-qt5-universal.pkg -o gnuplot-qt5-universal.pkg
    pkgutil --expand-full gnuplot-qt5-universal.pkg gnuplot_ROOT
    cp -a gnuplot_ROOT/gnuplot-package-*-qt5-universal.pkg/Payload/* vrecord_ROOT/usr/local/lib/vrecord
popd

#-----------------------------------------------------------------------
# Get MKVToolNix
# 42.0.0 is the last x86_64-only release with a low macOS requirement
# (10.14); upstream still ships no arm64 macOS build, so this runs under
# Rosetta 2 on Apple Silicon in every package.
pushd "${release_directory}/"
    curl -L https://mkvtoolnix.download/macos/releases/42.0.0/MKVToolNix-42.0.0.dmg -o MKVToolNix-42.0.0.dmg

    hdiutil attach -noverify MKVToolNix-42.0.0.dmg
    cp -a "/Volumes/MKVToolNix-42.0.0/MKVToolNix-42.0.0.app/Contents/MacOS/mkvextract" vrecord_ROOT/usr/local/lib/vrecord/bin
    cp -a "/Volumes/MKVToolNix-42.0.0/MKVToolNix-42.0.0.app/Contents/MacOS/mkvinfo" vrecord_ROOT/usr/local/lib/vrecord/bin
    cp -a "/Volumes/MKVToolNix-42.0.0/MKVToolNix-42.0.0.app/Contents/MacOS/mkvmerge" vrecord_ROOT/usr/local/lib/vrecord/bin
    cp -a "/Volumes/MKVToolNix-42.0.0/MKVToolNix-42.0.0.app/Contents/MacOS/mkvpropedit" vrecord_ROOT/usr/local/lib/vrecord/bin
    hdiutil detach "/Volumes/MKVToolNix-42.0.0"
popd

#-----------------------------------------------------------------------
# Get mpv
# stolendata ships discrete per-arch builds of the same version (the x86_64
# build needs macOS 11+, the arm64 build macOS 14+). The universal package
# bundles both apps behind a small arch-dispatching wrapper.
pushd "${release_directory}/"
    if [ "${ARCH}" = "arm64" ] ; then
        MPV_TARBALL="mpv-arm64-0.39.0.tar.gz"
    else
        MPV_TARBALL="mpv-0.39.0.tar.gz"
    fi
    curl -LO "https://laboratory.stolendata.net/~djinn/mpv_osx/${MPV_TARBALL}"
    tar -xf "${MPV_TARBALL}"
    if [ "${ARCH}" = "universal" ] ; then
        mv mpv.app vrecord_ROOT/usr/local/lib/vrecord/app/mpv-x86_64.app
        curl -LO "https://laboratory.stolendata.net/~djinn/mpv_osx/mpv-arm64-0.39.0.tar.gz"
        tar -xf mpv-arm64-0.39.0.tar.gz
        mv mpv.app vrecord_ROOT/usr/local/lib/vrecord/app/mpv-arm64.app
        cat > vrecord_ROOT/usr/local/lib/vrecord/bin/mpv << 'MPVWRAPPER'
#!/bin/sh
# run the bundled mpv build matching the host architecture
if [ "$(uname -m)" = "arm64" ] && [ -x "$(dirname "$0")/../app/mpv-arm64.app/Contents/MacOS/mpv" ] ; then
    exec "$(dirname "$0")/../app/mpv-arm64.app/Contents/MacOS/mpv" "$@"
else
    exec "$(dirname "$0")/../app/mpv-x86_64.app/Contents/MacOS/mpv" "$@"
fi
MPVWRAPPER
        chmod +x vrecord_ROOT/usr/local/lib/vrecord/bin/mpv
    else
        cp -a mpv.app vrecord_ROOT/usr/local/lib/vrecord/app
        ln -s ../app/mpv.app/Contents/MacOS/mpv vrecord_ROOT/usr/local/lib/vrecord/bin/mpv
    fi
popd

#-----------------------------------------------------------------------
# Get gtkdialog
# TODO: Build or get from any other source
pushd "${release_directory}/"
    curl -LO https://mediaarea.net/download/binary/gtkdialog/0.8.5/gtkdialog-0.8.5_mac.zip
    unzip gtkdialog-0.8.5_mac.zip
    cp -a gtkdialog/bin/gtkdialog vrecord_ROOT/usr/local/lib/vrecord/bin
    cp -a gtkdialog/lib/* vrecord_ROOT/usr/local/lib/vrecord/lib
popd

#-----------------------------------------------------------------------
# Build xmlstarlet
pushd "${release_directory}/"
    mkdir libxml2 libxslt xmlstarlet
    curl -LO https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.1.tar.xz
    tar -C libxml2 --strip-components 1 -xvf libxml2-2.15.1.tar.xz
    curl -LO https://download.gnome.org/sources/libxslt/1.1/libxslt-1.1.43.tar.xz
    tar -C libxslt --strip-components 1 -xvf libxslt-1.1.43.tar.xz
    curl -LO https://sourceforge.net/projects/xmlstar/files/xmlstarlet/1.6.1/xmlstarlet-1.6.1.tar.gz
    tar -C xmlstarlet --strip-components 1 -xvf xmlstarlet-1.6.1.tar.gz

    pushd libxml2
        ./configure ${CONFIGURE_HOST_FLAGS} --without-python --without-modules --without-iconv --without-icu --without-iso8859x --without-mem_debug --without-run_debug --with-regexps --with-tree --with-writer --with-pattern --with-push --with-valid --with-sax1 --with-legacy --enable-static --disable-shared
        make
    popd

    pushd libxslt
        ./configure ${CONFIGURE_HOST_FLAGS} --with-libxml-src="${release_directory}/libxml2" --without-python --without-modules --without-crypto --enable-static --disable-shared
        make
    popd

    pushd xmlstarlet
        # Apply Gentoo patches (vendored in Release/patches/xmlstarlet, SHA512
        # verified against the Gentoo Manifest) to fix compilation with
        # clang 16/17 and libxml2 >= 2.13. Gentoo removed app-text/xmlstarlet
        # from its repo, so the former live URLs now 404.
        # See https://bugs.gentoo.org/927838, 944765, 955674
        patch -p1 < "${release_directory}/patches/xmlstarlet/xmlstarlet-1.6.1-clang16.patch"
        patch -p1 < "${release_directory}/patches/xmlstarlet/xmlstarlet-1.6.1-clang17.patch"
        patch -p1 < "${release_directory}/patches/xmlstarlet/xmlstarlet-1.6.1-libxml2-2.14.0-compile.patch"
        patch -p1 < "${release_directory}/patches/xmlstarlet/xmlstarlet-1.6.1-libxml2-2.13-stdin.patch"
        # Seed the malloc/realloc cache vars: in cross mode configure cannot
        # run the malloc(0) test, assumes it fails, and defines malloc as
        # rpl_malloc, which breaks the link with undefined _rpl_malloc/
        # _rpl_realloc (fine on macOS where malloc(0) is nonnull).
        ac_cv_func_malloc_0_nonnull=yes ac_cv_func_realloc_0_nonnull=yes \
        ./configure ${CONFIGURE_HOST_FLAGS} --enable-static-libs --disable-build-docs --with-libxml-src="${release_directory}/libxml2" --with-libxslt-src="${release_directory}/libxslt"
        make || true # Build fails on docs despite --disable-build-docs, force the build to be marked as successful
    popd
    cp -a xmlstarlet/xml vrecord_ROOT/usr/local/lib/vrecord/bin/xmlstarlet
popd

#-----------------------------------------------------------------------
# Build deckcontrol
pushd "${release_directory}/"
    git clone --depth 1 https://github.com/bavc/deckcontrol.git
    if [ -z "${BMSDK}" ] ; then
        if ! (echo "#include <DeckLinkAPI.h>" | clang -E - >/dev/null 2>&1) ; then
            git clone --depth 1 https://github.com/MediaArea/decklink-sdk-headers.git
            BMSDK="${release_directory}/decklink-sdk-headers/DeckLinkSDK/Mac/include"
        fi
    fi
    pushd deckcontrol
        # The Makefile hardcodes CFLAGS/LDFLAGS (environment values are
        # ignored) and its += rules are dropped when variables are set on the
        # make command line, so pass the arch flags together with the pieces
        # those += rules would have added.
        make BMSDK="${BMSDK}" \
             CFLAGS="-g -mmacosx-version-min=11.0 -I${BMSDK} ${MACOS_ARCH_FLAGS}" \
             LDFLAGS="-lstdc++ -lpthread -ldl -g -framework CoreFoundation -mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS}"
    popd

    cp -a deckcontrol/deckcontrol vrecord_ROOT/usr/local/lib/vrecord/bin
popd

#-----------------------------------------------------------------------
# Build timecodexml
pushd "${release_directory}/"
    git clone --depth 1 https://github.com/MediaArea/timecodexml.git
    pushd timecodexml
        # The Makefile compiles with $(CXXFLAGS) $(CPPFLAGS) and links with
        # $(LDFLAGS) without assigning CPPFLAGS/LDFLAGS, so the arch flags
        # can be passed through those variables.
        make CPPFLAGS="-mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS}" LDFLAGS="-mmacosx-version-min=11.0 ${MACOS_ARCH_FLAGS}"
    popd

    cp -a timecodexml/timecodexml2webvtt vrecord_ROOT/usr/local/lib/vrecord/bin
popd

#-----------------------------------------------------------------------
# Bundle vrecord
pushd "${release_directory}/"
    cp -a ../vtest vrecord_ROOT/usr/local/bin
    cp -a ../vplay vrecord_ROOT/usr/local/bin
    cp -a ../vrecord vrecord_ROOT/usr/local/bin

    cp -a ../vtest.1 vrecord_ROOT/usr/local/share/man/man1
    cp -a ../vrecord.1 vrecord_ROOT/usr/local/share/man/man1

    cp -a ../Resources/audio_mode.gif vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/dvrecord.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/qcview.lua vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_preview_reader.c vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord-icon.svg vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord-dark.gtkrc vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_functions vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo_playback.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo_audio.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo_edit.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo_help.png vrecord_ROOT/usr/local/share/vrecord/Resources
    cp -a ../Resources/vrecord_logo_documentation.png vrecord_ROOT/usr/local/share/vrecord/Resources
popd

#-----------------------------------------------------------------------
# Sign binaries

cat - > "${release_directory}/vrecord.entitlements" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
EOF

pushd "${release_directory}/"
    if [ -n "${MACOS_CODESIGN_IDENTITY}" ] ; then
        find  vrecord_ROOT/usr/local -type f -print0 | while IFS= read -r -d '' f ; do
            if file "$f" | grep -q 'Mach-O.*\(executable\|dynamically linked shared library\)' ; then
                codesign --force --options runtime --timestamp --entitlements vrecord.entitlements --sign "Developer ID Application: ${MACOS_CODESIGN_IDENTITY}" "$f"
            fi
        done
        for mpv_app in vrecord_ROOT/usr/local/lib/vrecord/app/*.app ; do
            codesign --force --options runtime --timestamp --sign "Developer ID Application: ${MACOS_CODESIGN_IDENTITY}" --identifier "com.github.amiaopensource.vrecord" "${mpv_app}"
        done
    fi
popd

#-----------------------------------------------------------------------
# Package .pkg
pushd "${release_directory}/"
    pkgbuild --root vrecord_ROOT --identifier "com.github.amiaopensource.vrecord" --version "${version}" "vrecord.unsigned.pkg"
popd

#-----------------------------------------------------------------------
# Sign .pkg
pushd "${release_directory}/"
    if [ -n "${MACOS_CODESIGN_IDENTITY}" ] ; then
        productsign --sign "Developer ID Installer: ${MACOS_CODESIGN_IDENTITY}" "vrecord.unsigned.pkg" "vrecord.pkg"
    else
        mv -f "vrecord.unsigned.pkg" "vrecord.pkg"
    fi
popd

#-----------------------------------------------------------------------
# Package .dmg
pushd "${release_directory}/"
    tmp_path="$(mktemp -d)"
    trap "rm -rf ${tmp_path}" EXIT

    tmp_files="tmp-vrecord"
    tmp_dmg="tmp-vrecord.dmg"

    mkdir -p "${tmp_path}/${tmp_files}"
    cp -a "vrecord.pkg" "${tmp_path}/${tmp_files}/"

    hdiutil create "${tmp_path}/${tmp_dmg}" -ov -fs HFS+ -format UDRW -volname "vrecord" -srcfolder "${tmp_path}/${tmp_files}"
    hdiutil attach -readwrite -noverify "${tmp_path}/${tmp_dmg}"

    sleep 1

    # Finder styling is best-effort: Apple Events to Finder are blocked in
    # headless/CI/background contexts (AppleEvent timed out, -1712) and can
    # prompt for Automation permission interactively. Fall back to an
    # unstyled dmg rather than failing the whole release.
    echo '
        tell application "Finder"
            tell disk "vrecord"
                open
                set current view of container window to icon view
                set toolbar visible of container window to false
                set the bounds of container window to {400, 100, 950, 600}
                set viewOptions to the icon view options of container window
                set arrangement of viewOptions to not arranged
                set icon size of viewOptions to 72
                set position of item "vrecord.pkg" of container window to {125, 175}
                close
            end tell
        end tell
    ' | osascript || echo "warning: could not style dmg via Finder (AppleEvent blocked/unavailable); creating unstyled dmg"

    # Finder may still hold the volume busy after styling; retry then force.
    hdiutil detach "/Volumes/vrecord" || {
        sleep 2
        hdiutil detach "/Volumes/vrecord" || hdiutil detach -force "/Volumes/vrecord"
    }
    hdiutil convert "${tmp_path}/${tmp_dmg}" -format UDBZ -o "vrecord_${version}_Mac${ARCH_SUFFIX}.dmg"
popd

#-----------------------------------------------------------------------
# Sign .dmg
pushd "${release_directory}/"
    if [ -n "${MACOS_CODESIGN_IDENTITY}" ] ; then
        codesign --force --options runtime --timestamp --sign "Developer ID Application: ${MACOS_CODESIGN_IDENTITY}" --identifier "com.github.amiaopensource.vrecord" "vrecord_${version}_Mac${ARCH_SUFFIX}.dmg"
    fi
popd
