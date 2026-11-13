#!/usr/bin/env bash
# Installer + updater tests for the Windows packages (no network, no card):
#   bash Release/ci/windows-update-tests.sh <package dir> <portable zip> <setup.exe>
#
# A fake newer release (v9999.1.1) is described by a GitHub-style JSON whose
# asset URLs are file:// URLs to the real zip / setup, plus a SHA256SUMS.txt.
#  1. portable: check -> download -> SHA256 verify -> unpack
#  2. a wrong checksum is refused (and the download deleted)
#  3. portable --apply on a copy: shipped files replaced, user files kept
#  4. silent install (/S /D=) into a scratch folder: files, no portable.txt,
#     uninstall entry, Start Menu
#  5. the installed copy updates itself: --apply runs the setup silently
#  6. silent uninstall removes the folder, entry and Start Menu; leaves the
#     desktop alone
set -uo pipefail
PKG="$(cd "${1:?usage: windows-update-tests.sh <package dir> <portable zip> <setup.exe>}" && pwd)"
ZIP="$(cd "$(dirname "${2:?portable zip}")" && pwd)/$(basename "$2")"
SETUP="$(cd "$(dirname "${3:?setup.exe}")" && pwd)/$(basename "$3")"
WORK="$(mktemp -d)"
export MSYS2_ARG_CONV_EXCL='*' MSYS_NO_PATHCONV=1
FAILS=0
pass() { echo "PASS  $*" ; }
fail() { echo "FAIL  $*" ; FAILS=$((FAILS + 1)) ; }
win() { cygpath -w "$1" ; }
url() { echo "file:///$(cygpath -m "$1")" ; }
TAG="v9999.1.1"
UNINSTKEY='HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\vrecord'
# robocopy, not cp: to MSYS, "vrecord" (the backend script) and "Vrecord.exe"
# are the same name, so cp skips one of them
copy_tree() { robocopy "$(win "$1")" "$(win "$2")" /E /NFL /NDL /NJH /NJS /NP > /dev/null ; [[ $? -lt 8 ]] ; }
# the install helper's log: the app reports where it is (apply-log=...)
APPLY_LOG=""
wait_for_apply() {   # wait_for_apply <package dir>: until the helper logged "done" (max 180 s)
    APPLY_LOG="$(sed -n 's/^apply-log=//p' "$1/VrecordQt6-update-test.log" 2> /dev/null | tr -d '\r')"
    [[ -n "$APPLY_LOG" ]] || return 1
    APPLY_LOG="$(cygpath -u "$APPLY_LOG")"
    for _ in $(seq 1 360) ; do
        grep -q ' done$' "$APPLY_LOG" 2> /dev/null && return 0
        sleep 0.5
    done
    return 1
}
# update_test <exe> <json> [args...]: report in <exe dir>/VrecordQt6-update-test.log
update_test() {
    local exe="$1" json="$2" ; shift 2
    ( cd "$(dirname "$exe")" && /usr/bin/timeout -k 5 600 "./$(basename "$exe")" --update-test "$(url "$json")" "$@" > /dev/null 2>&1 )
}
report() { sed 's/^/    /' "$1/VrecordQt6-update-test.log" 2> /dev/null ; }
show_apply_log() { [[ -n "$APPLY_LOG" ]] && sed 's/^/    apply: /' "$APPLY_LOG" 2> /dev/null ; }

# ---- the fake release
REL="$WORK/release"
mkdir -p "$REL"
ZIP_NAME="vrecord_${TAG}_windows_x86_64_portable.zip"
SETUP_NAME="vrecord_${TAG}_windows_x86_64_setup.exe"
{
    echo "$(sha256sum "$ZIP" | cut -c1-64)  ${ZIP_NAME}"
    echo "$(sha256sum "$SETUP" | cut -c1-64)  ${SETUP_NAME}"
} > "$REL/SHA256SUMS.txt"
printf '%064d  %s\n' 0 "$ZIP_NAME" > "$REL/SHA256SUMS-bad.txt"
release_json() {   # release_json <sums file>
    cat << EOF
{ "tag_name": "${TAG}", "name": "vrecord ${TAG} (test)", "body": "test release",
  "draft": false, "prerelease": false, "html_url": "https://example.invalid/${TAG}",
  "assets": [
    { "name": "${ZIP_NAME}", "size": $(stat -c %s "$ZIP"), "browser_download_url": "$(url "$ZIP")" },
    { "name": "${SETUP_NAME}", "size": $(stat -c %s "$SETUP"), "browser_download_url": "$(url "$SETUP")" },
    { "name": "SHA256SUMS.txt", "size": 200, "browser_download_url": "$(url "$REL/$1")" } ] }
EOF
}
release_json SHA256SUMS.txt > "$REL/latest.json"
release_json SHA256SUMS-bad.txt > "$REL/latest-bad.json"

# ---- 1-3: portable
PORT="$WORK/portable"
copy_tree "$PKG" "$PORT" || fail "copy the package"
[[ -f "$PORT/portable.txt" ]] && pass "package is marked portable (portable.txt)" || fail "package has no portable.txt"
if update_test "$PORT/VrecordQt6.exe" "$REL/latest.json" && grep -q "downloaded, verified and unpacked" "$PORT/VrecordQt6-update-test.log" ; then
    pass "portable update: check, download, SHA256 verify, unpack"
else
    fail "portable update" ; report "$PORT"
fi
if update_test "$PORT/VrecordQt6.exe" "$REL/latest-bad.json" --expect-fail && grep -q "Checksum mismatch" "$PORT/VrecordQt6-update-test.log" ; then
    pass "a wrong SHA256 is refused"
else
    fail "wrong SHA256 not refused" ; report "$PORT"
fi
echo "OLD" > "$PORT/README-WINDOWS.txt"
echo "mine" > "$PORT/user-notes.txt"
if update_test "$PORT/VrecordQt6.exe" "$REL/latest.json" --apply && wait_for_apply "$PORT" \
   && grep -q "portable files copied: True" "$APPLY_LOG" \
   && [[ "$(head -1 "$PORT/README-WINDOWS.txt")" != "OLD" && -f "$PORT/user-notes.txt" ]] ; then
    pass "portable apply: files replaced after exit, user files kept"
else
    fail "portable apply" ; report "$PORT" ; show_apply_log
fi
[[ -n "$APPLY_LOG" ]] && rm -f "$APPLY_LOG"

# ---- 4-6: installer
INST="$WORK/installed/vrecord"
# shell folders by CSIDL (right even when the Desktop is redirected, e.g. OneDrive)
DESKTOP_LNK="$(cygpath -F 16)/vrecord.lnk"
START_MENU="$(cygpath -F 2)/vrecord"
DESKTOP_BEFORE="$([[ -e "$DESKTOP_LNK" ]] && echo yes || echo no)"
"$SETUP" /S "/D=$(win "$INST")"
rc=$?
if [[ $rc -eq 0 && -f "$INST/VrecordQt6.exe" && -f "$INST/vrecord" && -f "$INST/bin/ffmpeg-ma.exe" && -f "$INST/uninstall.exe" && ! -e "$INST/portable.txt" ]] ; then
    pass "silent install: files in place, portable.txt removed"
else
    fail "silent install (exit $rc)" ; ls "$INST" 2> /dev/null | head
fi
if ls "$INST"/VrecordQt6-*.log "$INST"/vrecord-qt6-update.ini > /dev/null 2>&1 ; then
    fail "installer shipped run logs / local settings"
else
    pass "installer ships no run logs or local settings"
fi
if reg query "$UNINSTKEY" /v DisplayVersion > /dev/null 2>&1 && [[ -f "$START_MENU/vrecord.lnk" ]] ; then
    pass "uninstall entry + Start Menu shortcut"
else
    fail "uninstall entry / Start Menu shortcut missing ($START_MENU)"
fi
echo "OLD" > "$INST/README-WINDOWS.txt"
if update_test "$INST/VrecordQt6.exe" "$REL/latest.json" --apply && grep -q "kind=installed" "$INST/VrecordQt6-update-test.log" \
   && wait_for_apply "$INST" && grep -q "setup exit code 0" "$APPLY_LOG" && [[ "$(head -1 "$INST/README-WINDOWS.txt")" != "OLD" ]] ; then
    pass "installed update: verified setup ran silently into the same folder"
else
    fail "installed update" ; report "$INST" ; show_apply_log
fi
# the uninstaller copies itself to %TEMP% and returns at once: wait for it
"$INST/uninstall.exe" /S
for _ in $(seq 1 120) ; do [[ -e "$INST" ]] || break ; sleep 0.5 ; done
if [[ ! -e "$INST" ]] && ! reg query "$UNINSTKEY" > /dev/null 2>&1 && [[ ! -e "$START_MENU" ]] \
   && ! reg query 'HKCU\Software\vrecord\vrecord-qt6\update' > /dev/null 2>&1 ; then
    pass "silent uninstall: folder, uninstall entry, Start Menu and update settings removed"
else
    fail "silent uninstall left something behind" ; ls "$INST" 2> /dev/null | head -5
fi
DESKTOP_AFTER="$([[ -e "$DESKTOP_LNK" ]] && echo yes || echo no)"
[[ "$DESKTOP_BEFORE" == "$DESKTOP_AFTER" ]] && pass "desktop shortcut left as it was ($DESKTOP_AFTER)" || fail "desktop shortcut changed ($DESKTOP_BEFORE -> $DESKTOP_AFTER)"

[[ -n "$APPLY_LOG" ]] && rm -rf "$(dirname "$APPLY_LOG")"
rm -rf "$WORK"
echo "=== ${FAILS} failure(s)"
exit $(( FAILS > 0 ? 1 : 0 ))
