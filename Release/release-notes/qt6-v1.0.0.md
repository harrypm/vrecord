**Vrecord 1.0.0 for Windows**: the Qt6 GUI release. This is the first semantically versioned release, and the first with separate Qt6 and GTK releases. The GTK interface for Windows, Linux and macOS is in the matching [`gtk-v1.0.0`](https://github.com/harrypm/vrecord/releases/tag/gtk-v1.0.0) release.

## Highlights

- **One name: Vrecord.** The program is `Vrecord.exe`. The window, the installer, the Start Menu and Apps & features all say *Vrecord*.
- **A leaner package.** The GTK interface now has its own release. This package carries only the Qt6 GUI, the capture backend and its tools.
- **Video Preview first.** Live input preview, file playout to the card, level meters and the test signal generator are on one tab, which the window opens on.
- **Live-editable timecode.** Type over the playout counter, or scroll over any part of it, and the output seeks straight away.
- **Version numbers.** Releases now use version numbers like 1.0.0, and the updater picks the newest Qt6 release.

## Download

| | Download | Notes |
|---|---|---|
| Windows 10 / 11 | [**Installer** (.exe)](@DL@/vrecord-qt6_1.0.0_windows_x86_64_setup.exe) | Installs for the current user into `%LOCALAPPDATA%\Programs\vrecord`, with Start Menu shortcuts. No administrator rights needed. |
| Windows, portable | [Portable (.zip)](@DL@/vrecord-qt6_1.0.0_windows_x86_64_portable.zip) | Unzip anywhere and run `Vrecord.exe`. |
| Checksums | [SHA256SUMS.txt](@DL@/SHA256SUMS.txt) | See *Verify a download* below. |

**Requirements:**
- Blackmagic **Desktop Video** drivers.
- **Git for Windows** or **MSYS2** (`C:\msys64`), which provide the bash that runs the capture script.

### Upgrading from 2026-11-25

Copies on 2026-11-25 cannot update themselves to 1.0.0: their updater predates the version change. Download the installer above once:
- It upgrades the install in place and keeps your settings.
- It removes the old `VrecordQt6.exe` and repoints an existing desktop shortcut to `Vrecord.exe`.

From 1.0.0 on, *Config → Updates* handles new releases again.

@SCREENSHOTS@

## What's new since 2026-11-25

- **Name**: the program is `Vrecord.exe` (it was `VrecordQt6.exe`). The window title, dialogs, installer and shortcuts all read *Vrecord*.
- **Packages**: the GTK interface moved to its own release. This package no longer carries gtkdialog or the GTK launcher, nor 27 GTK-only libraries.
- **Updater**:
  - It reads the release list and offers only published Qt6 releases. GTK releases and pre-releases are never offered.
  - It compares version numbers, not the `qt6-` prefix, and treats any 1.x version as newer than the old date versions.
- **Fixes**:
  - The Logs tab heading reads "Errors & Warnings". It showed "Errors _Warnings".
  - Tool versions on the Config tab are read as UTF-8, so mpv's "Copyright ©" no longer shows as "Â©".
- **Screenshots**: `Vrecord.exe --screenshots <folder>` saves one image per tab without opening a window. Every release now gets the gallery above.

## Known limitations

- **Not code-signed**: Windows SmartScreen may warn on first run. Choose *More info → Run anyway*.
- **WSS not yet tried on real hardware**: aspect detection has been tested against synthetic WSS lines, not yet a widescreen PAL capture. Reports are welcome.

## Verify a download

On Windows, compare the output of `certutil -hashfile <file> SHA256` with the matching line in [`SHA256SUMS.txt`](@DL@/SHA256SUMS.txt). Elsewhere:

```
sha256sum -c SHA256SUMS.txt --ignore-missing
```
