**Vrecord 1.0.0 with the GTK interface** for Windows, Linux and macOS: the classic Vrecord window. This is the first semantically versioned release, and the first with separate GTK and Qt6 releases. The Windows Qt6 GUI is in the matching [`qt6-v1.0.0`](https://github.com/harrypm/vrecord/releases/tag/qt6-v1.0.0) release.

## Highlights

- **GTK on every platform**: Windows (portable), Debian / Ubuntu (`.deb`), any x86_64 Linux (AppImage), and macOS (Apple silicon, Intel or universal).
- **Test signal on Video Preview**: the test pattern and tone controls moved from the old Output tab to the output side of the Video Preview tab, next to *Relay live feed*.
- **Same layout everywhere**: the Video Preview tab matches the Linux / macOS layout, and fills the window instead of sitting off to one side.
- **Windows fix**: the package always uses its own dialog program, so an older install can no longer break the device lists.
- **Version numbers**: releases now use version numbers like 1.0.0.

## Download

| System | Download | Notes |
|---|---|---|
| Windows 10 / 11 | [Portable (.zip)](@DL@/vrecord-gtk_1.0.0_windows_x86_64_portable.zip) | Unzip anywhere and run `Vrecord.exe`. Needs Blackmagic Desktop Video and Git for Windows or MSYS2. |
| Debian / Ubuntu | [**.deb** package](@DL@/vrecord-gtk_1.0.0_linux_amd64.deb) | `sudo apt install ./vrecord-gtk_1.0.0_linux_amd64.deb` |
| Any x86_64 Linux | [AppImage](@DL@/vrecord-gtk_1.0.0_linux_x86_64.AppImage) | `chmod +x`, then run. |
| macOS, Apple silicon | [**.dmg**](@DL@/vrecord-gtk_1.0.0_macos_arm64.dmg) · [.app.zip](@DL@/vrecord-gtk_1.0.0_macos_arm64.app.zip) | |
| macOS, Intel | [**.dmg**](@DL@/vrecord-gtk_1.0.0_macos_x86_64.dmg) · [.app.zip](@DL@/vrecord-gtk_1.0.0_macos_x86_64.app.zip) | |
| macOS, either | [**.dmg**](@DL@/vrecord-gtk_1.0.0_macos_universal.dmg) · [.app.zip](@DL@/vrecord-gtk_1.0.0_macos_universal.app.zip) | Universal: one app for both. |
| Checksums | [SHA256SUMS.txt](@DL@/SHA256SUMS.txt) | See *Verify a download* below. |

On macOS and Linux, Vrecord can also be installed with Homebrew from the AMIA Open Source tap. See the [installation guide](https://github.com/harrypm/vrecord/blob/main/Resources/Documentation/installation_and_setup.md).

## What's new since 2026-11-25

- **Video Preview**: the test signal controls (*Test signal output*, test pattern, test audio) are on the output side of Video Preview. The separate Output tab is gone.
- **Windows**:
  - Vrecord always uses the dialog program shipped beside it. A cached path to an older package's dialog had shown a bash error in the *Relay output device* list and pushed the page far wider than the window.
  - The package carries only the GTK interface; the Qt6 GUI and 23 Qt-only libraries moved to the Qt6 release.
- **Linux**:
  - `timecodexml2webvtt` is bundled with the other tools.
  - The `.deb` version is `1:1.0.0`, so apt treats it as an upgrade from the date-versioned packages.
- **File names**: every file is named `vrecord-gtk_<version>_<platform>_<arch>`.

## Known limitations

- **Not code-signed.**
  - On macOS, the first time, right-click the app and choose *Open*.
  - Windows SmartScreen may warn: choose *More info → Run anyway*.
- **Intel-only helper tools on Apple silicon**: gtkdialog, MKVToolNix and dvrescue have no arm64 builds, so the arm64 and universal packages include their x86_64 versions, which run under Rosetta 2.
- **No automatic updates**: new GTK versions are downloaded by hand from the releases page.

## Verify a download

```
sha256sum -c SHA256SUMS.txt --ignore-missing
```
On Windows, compare the output of `certutil -hashfile <file> SHA256` with the matching line in [`SHA256SUMS.txt`](@DL@/SHA256SUMS.txt).
