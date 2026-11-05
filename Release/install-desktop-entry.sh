#!/bin/sh
# installs a vrecord menu/taskbar entry for the current user (no root needed):
# a placeholder icon into the hicolor theme and a .desktop entry that runs
# vrecord in a terminal. The GUI window gets the vrecord icon in the taskbar
# through StartupWMClass=gtkdialog.

VRECORD_BIN="$(command -v vrecord)"
if [ -z "${VRECORD_BIN}" ] ; then
    echo "vrecord was not found on PATH - install vrecord first" >&2
    exit 1
fi
VRECORD_REAL="$(readlink -f "${VRECORD_BIN}" 2>/dev/null || echo "${VRECORD_BIN}")"

SCRIPTDIR="$(dirname "$0")"
ICON_SOURCE=""
if [ -f "${SCRIPTDIR}/../Resources/vrecord-icon.svg" ] ; then
    ICON_SOURCE="${SCRIPTDIR}/../Resources/vrecord-icon.svg"
elif [ -f "$(dirname "$(dirname "${VRECORD_REAL}")")/vrecord-icon.svg" ] ; then
    # installed layout: the icon sits next to vrecord_functions in the install prefix
    ICON_SOURCE="$(dirname "$(dirname "${VRECORD_REAL}")")/vrecord-icon.svg"
fi
if [ -z "${ICON_SOURCE}" ] ; then
    echo "could not find Resources/vrecord-icon.svg" >&2
    exit 1
fi

ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"
APPS_DIR="${HOME}/.local/share/applications"
mkdir -p "${ICON_DIR}" "${APPS_DIR}"
cp "${ICON_SOURCE}" "${ICON_DIR}/vrecord.svg"

cat > "${APPS_DIR}/vrecord.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=vrecord
GenericName=Video Capture
Comment=Capture video signals to digital files
Exec=${VRECORD_BIN}
Icon=vrecord
Terminal=true
Categories=AudioVideo;Video;Recorder;
StartupWMClass=gtkdialog
DESKTOP

if command -v update-desktop-database >/dev/null 2>&1 ; then
    update-desktop-database "${APPS_DIR}" 2>/dev/null
fi
echo "installed ${APPS_DIR}/vrecord.desktop"
echo "installed ${ICON_DIR}/vrecord.svg"
echo "vrecord should now appear in the menu under Sound & Video"
