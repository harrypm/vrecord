#!/usr/bin/env bash
# Tests for the built Windows portable package (no capture hardware needed).
#   bash Release/ci/windows-package-tests.sh <package dir>
# Exits non-zero if any test fails; prints a PASS/FAIL line per test.
set -uo pipefail
PKG="$(cd "${1:?usage: windows-package-tests.sh <package dir>}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$(mktemp -d)"
export MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1
FAILS=0
pass() { echo "PASS  $*" ; }
fail() { echo "FAIL  $*" ; FAILS=$((FAILS + 1)) ; }
# run a packaged tool with only the package and Windows on PATH: proves it is
# self-contained (no MSYS2 DLLs leaking in from the build environment)
isolated() { /usr/bin/env -i PATH="$PKG/bin:$PKG:/c/Windows/System32:/c/Windows" SYSTEMROOT='C:\Windows' "$@" ; }

echo "=== package: $PKG"

# ---- scripts parse
for script in "$PKG/vrecord" "$PKG/bin/dvpackager" ; do
    if bash -n "$script" ; then pass "bash -n $(basename "$script")" ; else fail "bash -n $(basename "$script")" ; fi
done

# ---- ffmpeg-ma features vrecord depends on (read whole lists: no grep -q pipes)
DEVICES="$(isolated "$PKG/bin/ffmpeg-ma.exe" -hide_banner -devices 2>&1)"
FILTERS="$(isolated "$PKG/bin/ffplay-ma.exe" -hide_banner -filters 2>&1)"
[[ "$DEVICES" == *" decklink "* ]] && pass "ffmpeg-ma has decklink in/out" || fail "ffmpeg-ma has decklink in/out"
[[ "$FILTERS" == *" drawtext "* ]] && pass "ffplay-ma has drawtext" || fail "ffplay-ma has drawtext"

# ---- every bundled tool starts from the package alone
tool() {   # tool <label> <expected text> <command...>
    local label="$1" expect="$2" ; shift 2
    local out
    out="$(isolated /usr/bin/timeout 30 "$@" 2>&1 | head -20)"
    if [[ "$out" == *"$expect"* ]] ; then pass "runs standalone: $label" ; else fail "runs standalone: $label -> $(echo "$out" | head -2 | tr '\n' ' ')" ; fi
}
tool ffmpeg-ma    "ffmpeg version"        "$PKG/bin/ffmpeg-ma.exe" -hide_banner -version
tool ffplay-ma    "ffplay version"        "$PKG/bin/ffplay-ma.exe" -hide_banner -version
tool ffprobe-ma   "ffprobe version"       "$PKG/bin/ffprobe-ma.exe" -hide_banner -version
tool MediaInfo    "MediaInfo"             "$PKG/bin/MediaInfo.exe" --Version
tool MediaConch   "MediaConch"            "$PKG/bin/MediaConch.exe" --version
tool DVRescue     "DVRescue"              "$PKG/bin/DVRescue.exe" --version
# mpv.exe is a GUI-subsystem program (no console output): mpv.com is the
# console wrapper that prints, so the version check uses it when present
MPV_FAILS_BEFORE=$FAILS
if [[ -f "$PKG/bin/mpv.com" ]] ; then
    tool mpv      "mpv"                   "$PKG/bin/mpv.com" --version
else
    tool mpv      "mpv"                   "$PKG/bin/mpv.exe" --version
fi
if [[ "$FAILS" -gt "$MPV_FAILS_BEFORE" ]] ; then
    # a silent exit is usually a missing DLL: list what the loader cannot find
    for exe in "$PKG/bin/mpv.exe" "$PKG/bin/mpv.com" ; do
        [[ -f "$exe" ]] || continue
        isolated "$exe" --version > /dev/null 2>&1 ; echo "  $(basename "$exe") exit=$?"
        objdump -p "$exe" 2>/dev/null | grep -m1 -i 'subsystem' | sed 's/^/  /'
        ( cd "$PKG/bin" && PATH="$PKG/bin:/c/Windows/System32" ldd "$exe" 2>&1 | grep -i -E 'not found|\?\?\?' | sed 's/^/  missing: /' )
    done
fi
tool xmlstarlet   "1.6"                   "$PKG/bin/xml.exe" --version
tool deckcontrol  "getcurrentstate"       "$PKG/bin/deckcontrol.exe"
tool mkvpropedit  "mkvpropedit"           "$PKG/bin/mkvpropedit.exe" --version
if [[ -f "$PKG/bin/gtkdialog.exe" ]] ; then
    tool gtkdialog "gtkdialog"            "$PKG/bin/gtkdialog.exe" --version
else
    fail "gtkdialog.exe packaged"
fi

# ---- dvpackager end to end on a synthetic PAL DV file (MKV, and MOV with the
# split options vrecord passes): needs ffmpeg-ma, dvrescue, mediainfo, xmlstarlet
DVDIR="$WORK/dv"
mkdir -p "$DVDIR"
DV="$(cygpath -m "$DVDIR/dvtest.dv")"
if PATH="$PKG/bin:$PATH" ffmpeg-ma.exe -hide_banner -loglevel error -f lavfi -i testsrc=size=720x576:rate=25 \
        -f lavfi -i "sine=frequency=1000:sample_rate=48000" -t 5 -target pal-dv -y "$DV" ; then
    for case in "mkv:" "mov:-s -d -t" ; do
        ext="${case%%:*}" ; opts="${case#*:}"
        rm -f "$DVDIR"/dvtest_part*.* "$DVDIR"/dvtest.dv.dvrescue.xml
        # shellcheck disable=SC2086
        PATH="$PKG/bin:$PATH" /usr/bin/timeout 300 bash "$PKG/bin/dvpackager" \
            -F "$PKG/bin/ffmpeg-ma.exe" -M "$PKG/bin/MediaInfo.exe" -D "$PKG/bin/DVRescue.exe" -X "$PKG/bin/xml.exe" \
            $opts -e "$ext" "$DV" > "$WORK/dvpackager-$ext.log" 2>&1
        out="$(ls "$DVDIR"/dvtest_part1."$ext" 2>/dev/null)"
        frames="$(PATH="$PKG/bin:$PATH" ffprobe-ma.exe -v error -count_frames -select_streams v:0 \
                  -show_entries stream=nb_read_frames -of csv=p=0 "$(cygpath -m "${out:-/nonexistent}")" 2>/dev/null | tr -d '\r' | grep -m1 .)"
        if [[ -n "$out" && "$frames" == "125" ]] ; then
            pass "dvpackager DV -> $ext ($frames frames${opts:+, $opts})"
        else
            fail "dvpackager DV -> $ext (frames='${frames}', output='${out}')"
            echo "  --- dvpackager-$ext log:" ; sed 's/^/  /' "$WORK/dvpackager-$ext.log" | head -60
            echo "  --- $DVDIR:" ; ls -la "$DVDIR" | sed 's/^/  /'
        fi
    done
else
    fail "make synthetic DV test file"
fi

# ---- Qt6 frontend self-test (offscreen; checks that need a card SKIP in CI)
if [[ -f "$PKG/VrecordQt6.exe" ]] ; then
    ( cd "$PKG" && QT_QPA_PLATFORM=offscreen VRECORD_SELFTEST_ALLOW_NO_HARDWARE=1 \
        /usr/bin/timeout 300 ./VrecordQt6.exe --self-test > "$WORK/selftest-console.txt" 2>&1 )
    rc=$?
    echo "--- self-test log:"
    cat "$PKG/VrecordQt6-selftest.log" 2>/dev/null || echo "(no log)"
    if [[ $rc -ne 0 ]] ; then
        echo "--- self-test console (progress; the last line is where it stopped):"
        tail -40 "$WORK/selftest-console.txt"
        echo "--- startup log:"
        cat "$PKG/VrecordQt6-startup.log" 2>/dev/null
    fi
    if [[ $rc -eq 0 && -f "$PKG/VrecordQt6-selftest.log" ]] ; then pass "VrecordQt6 --self-test" ; else fail "VrecordQt6 --self-test (exit $rc)" ; fi
else
    fail "VrecordQt6.exe packaged"
fi

echo "=== ${FAILS} failure(s)"
exit $(( FAILS > 0 ? 1 : 0 ))
