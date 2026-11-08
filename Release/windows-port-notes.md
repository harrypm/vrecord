# vrecord Windows Port Notes

Status (2026-11-08, harrypm/vrecord `windows-port` branch on this station):
**Working portable package** with Intensity Pro 4K + Desktop Video 16.0.

## What works (verified hard data)
- `ffmpeg-ma` 9.0.1 (MediaArea patch set eea3be9) built under MSYS2 UCRT64 with `--enable-decklink`
- `-sources decklink` lists: `Intensity Pro 4K`
- Direct 2s capture from the card wrote `card.mkv` (NTSC 720x486 FFV1 + PCM)
- `vrecord -s smoketest` wrote `smoketest.mkv` (8s FFV1 10-bit Matroska) after Windows path conversion
- gtkdialog 0.8.5 built against GTK+ 3
- xmlstarlet, MediaInfo CLI, mkvtoolnix tools bundled

## Package layout
- `Release/build-windows-portable.sh` → `Release/artifacts/vrecord_windows_<VERSION>/` + zip
- `Release/build-windows-installer.sh` → NSIS script (+ setup.exe if `makensis` is installed)
- Launch with `vrecord.cmd` (needs MSYS2 or Git Bash) or `vrecord.sh` from MSYS2

## Runtime requirements (only system install)
- Blackmagic **Desktop Video** drivers (this station: 16.0.0.0)
- MSYS2 bash (`C:\msys64`) or Git Bash for the launcher

## Script changes (minimal)
- `vrecord` `_setup_env_variables`: MINGW/MSYS/Cygwin → `OS_TYPE=windows`
- `_windows_native_path`: convert MSYS paths to `C:/...` for native MinGW ffmpeg
- `Resources/vrecord_functions`: Windows preview uses image2 update path (no POSIX fifo); dshow audio device list

## DeckLink headers
- Official BMD SDK zip is behind a JS form; used `MartinPulec/desktopvideo_sdk-api` v16.0
- Windows headers from release asset `deck-win-api.zip` + shim `DeckLinkAPI_v14_2_1.h`
- MediaArea patch 0015 `IID_IUnknown` needed a Windows GUID-layout fix in the build tree

## Known gaps / next
- Preview reader C helper needs POSIX `sys/select.h` (shell/image fallback used on Windows)
- awk division-by-zero warning during some option reviews (non-fatal)
- Full GUI (`vrecord -e`) needs user confirmation on this station
- Real tape/source capture needs user confirmation of physical input (Composite/HDMI/etc.)
- NSIS installer needs `makensis` installed to produce the setup exe
- Bundle a minimal standalone bash runtime to drop the MSYS2 dependency (future)

## Options originally considered
1. WSL2 + X — **not viable** for PCIe Intensity Pro 4K (no passthrough)
2. MSYS2 recompile — **chosen and working**
3. Full native rewrite — not required for this milestone
