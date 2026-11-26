#!/usr/bin/env bash
# Render a release's notes with its screenshot gallery.
#   render-release-notes.sh <notes.md> <tag> <repo> <screenshot file>... > out.md
# Replaces the @SCREENSHOTS@ line in <notes.md> with a "Screenshots" section:
# a two-column table showing each screenshot from the release's own
# downloads (the files are uploaded as release assets under the same names;
# click one for full size). With no screenshots the line is removed.
set -euo pipefail
NOTES="${1:?notes file}" ; TAG="${2:?tag}" ; REPO="${3:?owner/repo}" ; shift 3

# screenshot-01-video-preview.png -> Video Preview (DV, DeckLink spelled right)
title_of() {
    basename "$1" | sed -E 's/^screenshot-[0-9]+-//; s/\.png$//; s/-/ /g' \
        | awk '{ for (i = 1; i <= NF; i++) $i = toupper(substr($i, 1, 1)) substr($i, 2); print }' \
        | sed -E 's/\bDv\b/DV/g; s/Decklink/DeckLink/g'
}
cell() {
    local name title
    name="$(basename "$1")" ; title="$(title_of "$1")"
    printf '<a href="https://github.com/%s/releases/download/%s/%s"><img src="https://github.com/%s/releases/download/%s/%s" alt="%s"></a>' \
        "$REPO" "$TAG" "$name" "$REPO" "$TAG" "$name" "$title"
}
gallery() {
    [[ $# -gt 0 ]] || return 0
    echo "## Screenshots"
    echo
    echo "The Qt6 GUI on Windows, with default settings. Click any image for full size."
    echo
    local files=("$@") i left right
    for (( i = 0; i < ${#files[@]}; i += 2 )) ; do
        left="${files[i]}" ; right="${files[i + 1]:-}"
        echo "| $(title_of "$left") | ${right:+$(title_of "$right")} |"
        echo "|---|---|"
        echo "| $(cell "$left") | ${right:+$(cell "$right")} |"
        echo
    done
}

# placeholders on any line: @TAG@ -> the tag, @DL@ -> the release's
# download base (https://github.com/<repo>/releases/download/<tag>), so a
# notes file links its own downloads
DL="https://github.com/${REPO}/releases/download/${TAG}"
expand() { sed -e "s|@DL@|${DL}|g" -e "s|@TAG@|${TAG}|g" ; }

if grep -q '^@SCREENSHOTS@$' "$NOTES" ; then
    GALLERY="$(gallery "$@")"
    while IFS= read -r line || [[ -n "$line" ]] ; do
        if [[ "$line" == "@SCREENSHOTS@" ]] ; then
            [[ -n "$GALLERY" ]] && printf '%s\n' "$GALLERY"
        else
            printf '%s\n' "$line" | expand
        fi
    done < "$NOTES"
else
    expand < "$NOTES"
    [[ $# -gt 0 ]] && { echo ; gallery "$@" ; }
fi
