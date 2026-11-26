# Building Vrecord

How the packages are built, tested and released. To install or use Vrecord instead, see the [README](README.md).

## What gets built

Each version is released twice: once for each interface.

| Release | Interface | Packages |
|---|---|---|
| `qt6-v<version>` | Qt6 GUI (Windows) | `vrecord-qt6_<version>_windows_x86_64_setup.exe`, `vrecord-qt6_<version>_windows_x86_64_portable.zip` |
| `gtk-v<version>` | GTK interface | `vrecord-gtk_<version>_windows_x86_64_portable.zip`, `vrecord-gtk_<version>_linux_amd64.deb`, `vrecord-gtk_<version>_linux_x86_64.AppImage`, `vrecord-gtk_<version>_macos_<x86_64\|arm64\|universal>.dmg` / `.app.zip` |

Every release has its own `SHA256SUMS.txt`. In both packages the program is called **Vrecord**:
- In the Qt6 package, `Vrecord.exe` is the Qt6 GUI.
- In the GTK package, `Vrecord.exe` is the launcher for the GTK interface.

**Version** is the `VERSION="x.y.z"` line of the `vrecord` script, a semantic version. The capture script, the Qt6 build (read by `qt6/CMakeLists.txt`), the packages and the updater all use it. Releases before 1.0.0 used date versions (`2026-11-25`); the updater treats those as older than any semantic version.

## Windows (MSYS2 UCRT64)

All Windows building happens in an **MSYS2 UCRT64** shell.

### 1. Tools and libraries

```
pacman -S --needed git make autoconf automake libtool bison flex texinfo patch diffutils zip unzip p7zip perl
pacman -S --needed $(printf 'mingw-w64-ucrt-x86_64-%s ' toolchain cmake ninja pkgconf nasm \
    qt6-base qt6-svg qt6-tools SDL2 x264 openh264 freetype harfbuzz fontconfig fribidi \
    gtk3 libxml2 libxslt nsis opus)
```

`opus:p` is what gives ffmpeg-ma `libopus`. Without it, Opus recording falls back to ffmpeg's built-in encoder.

### 2. Build inputs

```
VRECORD_BUILD=$HOME/vrecord-build bash Release/ci/windows-build-deps.sh
```

This builds everything the package bundles from pinned, checksummed sources: ffmpeg-ma with DeckLink, drawtext and libopus; gtkdialog with Vrecord's Windows patch; xmlstarlet; deckcontrol; MediaInfo; MediaConch; DVRescue; mpv; MKVToolNix; fonts and cowsay. Steps whose output already exists are skipped. The DeckLink SDK headers are needed for ffmpeg-ma and deckcontrol.

### 3. Packages

```
bash Release/build-windows-portable.sh     # builds the Qt6 GUI and assembles the packages
bash Release/build-windows-installer.sh    # NSIS installer from the Qt6 package
```

Output in `Release/artifacts/`:

| Folder / file | What it is |
|---|---|
| `vrecord_windows_<version>/` | Combined **development** folder: both interfaces, the Qt6 app as `VrecordQt6.exe`. Not released. |
| `vrecord-qt6_windows_<version>/` + `vrecord-qt6_<version>_windows_x86_64_portable.zip` | The Qt6 package. |
| `vrecord-gtk_windows_<version>/` + `vrecord-gtk_<version>_windows_x86_64_portable.zip` | The GTK package. |
| `vrecord-qt6_<version>_windows_x86_64_setup.exe` | The Qt6 installer (per user, no administrator rights). |

Both packages are cut from the combined folder. A DLL leaves a package only when the removed programs needed it and nothing that stays does: checked with `ldd`, so a shared tool never loses a library.

Building only the Qt6 GUI (for example while developing) needs no packaging:

```
cmake -S qt6 -B <build dir> -G Ninja -DCMAKE_BUILD_TYPE=Release -DDECKLINK_INCLUDE_DIR=<decklink-include>
cmake --build <build dir>
```

Close any running Vrecord before replacing its exe: Windows locks a running program's file.

### 4. Tests (no capture hardware needed)

```
bash Release/ci/windows-package-tests.sh Release/artifacts/vrecord-qt6_windows_<version>
bash Release/ci/windows-package-tests.sh Release/artifacts/vrecord-gtk_windows_<version>
bash Release/ci/windows-update-tests.sh  Release/artifacts/vrecord-qt6_windows_<version> \
     Release/artifacts/vrecord-qt6_<version>_windows_x86_64_portable.zip \
     Release/artifacts/vrecord-qt6_<version>_windows_x86_64_setup.exe
```

- **Package tests**: every bundled tool starts with only the package on `PATH`; ffmpeg-ma's DeckLink, drawtext and audio encoders work; dvpackager runs end to end. In the Qt6 package they also run `Vrecord.exe --self-test`, offscreen. Set `VRECORD_TEST_SKIP_SELFTEST=1` when a running Vrecord has the card open.
- **Update tests**: the updater (check, download, SHA256 check, apply), the silent installer and the uninstaller, against a fake local release.
- **Screenshots**: `QT_QPA_PLATFORM=offscreen Vrecord.exe --screenshots <folder>` saves one PNG per tab. They use default settings, and any path under the home folder shows as `~`.

## Linux

The `.deb` and the AppImage bundle the GTK interface with Homebrew-built tools (Ubuntu 24.04 glibc). See `.github/workflows/build-linux.yml` for the full dependency steps:
- **apt**: GTK, DV and video tool packages.
- **Homebrew**: `ffmpeg-ma`, `cowsay`, `timecodexml`, `decklinksdk`, `gtkdialog`.
- **Built from source**: deckcontrol and dvrescue, outside Homebrew.

```
./Release/build-deb.sh         # Release/artifacts/vrecord-gtk_<version>_linux_<arch>.deb
./Release/build-appimage.sh    # Release/artifacts/vrecord-gtk_<version>_linux_x86_64.AppImage
```

The `.deb` version carries the epoch `1:` (`1:1.0.0`), so apt treats it as newer than the older date-versioned packages.

## macOS

```
brew install bison flex texinfo libxml2 libxslt automake autoconf
ARCH=arm64 ./Release/Release_Mac.sh      # x86_64, arm64 or universal -> vrecord-gtk_<version>_macos_<arch>.dmg
ARCH=arm64 ./Release/build-mac-app.sh    # -> Release/artifacts/vrecord-gtk_<version>_macos_<arch>.app.zip
```

gtkdialog, MKVToolNix and dvrescue have no arm64 builds, so every macOS package uses their x86_64 versions, which run under Rosetta 2 on Apple silicon. Signing needs `MACOS_CODESIGN_IDENTITY` (CI builds are unsigned).

## Continuous integration

| Workflow | Runs on | Does |
|---|---|---|
| `build-and-test.yml` | every branch push and pull request | Builds and tests all platforms and uploads the packages as run artifacts. |
| `build-and-release.yml` | `v*` tags, or a manual run | The same builds, then creates the Qt6 and GTK releases. |
| `build-windows.yml`, `build-linux.yml`, `build-macos.yml` | called by both of the above | One platform each. |

The Windows job caches the build inputs, keyed on the recipe and the week.

## Releasing

1. **Version**: set `VERSION="x.y.z"` in `vrecord` and commit.
2. **Notes**: add `Release/release-notes/qt6-vx.y.z.md` and `Release/release-notes/gtk-vx.y.z.md`. They support three placeholders:
   - `@TAG@` becomes the release tag;
   - `@DL@` becomes the release's download address, so `[Installer](@DL@/vrecord-qt6_x.y.z_windows_x86_64_setup.exe)` links the file;
   - a line holding only `@SCREENSHOTS@` becomes the screenshot gallery (Qt6).

   Without a notes file, GitHub's generated notes are used.
3. **Tag**: `git tag -a vx.y.z -m "Vrecord x.y.z"` and `git push origin vx.y.z`. The tag must match `VERSION`.
   - `vx.y.z` creates **draft** releases `qt6-vx.y.z` and `gtk-vx.y.z`. Check them, then publish both.
   - `vx.y.z-rc1` (any `-suffix`) publishes **pre-releases** straight away.
4. **Updates**: installed Qt6 copies offer the newest published, non-pre-release `qt6-v*` release (Config → Updates). The GTK packages are updated by hand.

A manual run (*Actions → Build and Release → Run workflow*) with *create_release* and a tag name creates a missing tag on the chosen branch and releases it.

## More

- [Windows port notes](Release/windows-port-notes.md): the Qt6 GUI's design, hardware findings and test tools.
- [Contribution guide](CONTRIBUTING.md)
