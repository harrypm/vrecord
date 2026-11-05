#!/bin/sh
# installs a vrecord menu/taskbar entry for the current user (no root needed):
# the placeholder icon into the hicolor theme (SVG plus PNG sizes) and a
# .desktop entry that runs vrecord in a terminal. The GUI window gets the
# vrecord icon in the taskbar through StartupWMClass=Gtkdialog, which matches
# gtkdialog's WM_CLASS res_class (verified with xprop).

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

ICONS_DIR="${HOME}/.local/share/icons/hicolor"
APPS_DIR="${HOME}/.local/share/applications"
mkdir -p "${ICONS_DIR}/scalable/apps" "${APPS_DIR}"
cp "${ICON_SOURCE}" "${ICONS_DIR}/scalable/apps/vrecord.svg"

# PNG fallback sizes help icon lookups that do not handle SVG
if command -v convert >/dev/null 2>&1 ; then
    for SIZE in 48 64 128 256 ; do
        mkdir -p "${ICONS_DIR}/${SIZE}x${SIZE}/apps"
        convert -background none "${ICON_SOURCE}" -resize ${SIZE}x${SIZE} \
            "${ICONS_DIR}/${SIZE}x${SIZE}/apps/vrecord.png" 2>/dev/null
    done
fi

# also install the icon under the gtkdialog name so a window-icon lookup by
# WM class (the taskbar's last resort for windows without their own icon)
# finds it even if the .desktop matching fails
cp "${ICONS_DIR}/scalable/apps/vrecord.svg" "${ICONS_DIR}/scalable/apps/gtkdialog.svg" 2>/dev/null
for SIZE in 48 64 128 256 ; do
    if [ -f "${ICONS_DIR}/${SIZE}x${SIZE}/apps/vrecord.png" ] ; then
        cp "${ICONS_DIR}/${SIZE}x${SIZE}/apps/vrecord.png" "${ICONS_DIR}/${SIZE}x${SIZE}/apps/gtkdialog.png"
    fi
done

cat > "${APPS_DIR}/vrecord.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=vrecord
GenericName=Video Capture
Comment=Capture video signals to digital files
Exec=${VRECORD_BIN}
Icon=vrecord
Terminal=false
Categories=AudioVideo;Video;Recorder;
StartupWMClass=Gtkdialog
DESKTOP

if command -v update-desktop-database >/dev/null 2>&1 ; then
    update-desktop-database "${APPS_DIR}" 2>/dev/null
fi
# rebuild the icon cache so the menu and taskbar pick the icon up immediately
if command -v gtk-update-icon-cache >/dev/null 2>&1 ; then
    gtk-update-icon-cache -f -t "${ICONS_DIR}" >/dev/null 2>&1
fi
echo "installed ${APPS_DIR}/vrecord.desktop"
echo "installed icons under ${ICONS_DIR} (scalable + PNG sizes)"
echo "vrecord should now appear in the menu under Sound & Video"
