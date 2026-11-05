# vrecord Windows Port Notes

vrecord does not currently run natively on Windows. This document lays out
what a Windows port would need, so the CI scaffolding in
`.github/workflows/build-windows.yml` has a clear target.

## Current architecture (why it doesn't port trivially)

- **vrecord** is a ~4000-line bash script — no Windows-native shell equivalent
- **GUI**: gtkdialog (GTK2) — no Windows build of gtkdialog exists
- **Capture**: ffmpeg-ma with `decklink` demuxer/muxer (Blackmagic SDK on
  macOS/Linux; the Windows DeckLink SDK exists but ffmpeg-ma doesn't ship a
  Windows build with it enabled)
- **Playback**: ffplay / mpv (both have Windows builds — this part ports)
- **Post-processing**: ffprobe, mediaconch, mkvtoolnix, xmlstarlet, gnuplot,
  cowsay (most have Windows builds)

## Options for a Windows port (in order of effort)

### 1. WSL2 + X server (lowest effort, "works today")
Package a WSL2 setup script + VcXsrv/X410 config. The Linux .deb or
AppImage runs inside WSL2 with GUI forwarded to the Windows X server.
Blackmagic drivers need to be installed on Windows AND passed through to
WSL2 (USB passthrough). The `build-windows.yml` workflow could produce a
WSL bootstrap installer instead of native binaries.

### 2. MSYS2/WSL recompile (medium effort)
Recompile the dependency chain under MSYS2 (bash, GTK2, ffmpeg with
DeckLink Windows SDK, gtkdialog). gtkdialog would need porting to GTK
on Windows (GTK2 runs on Windows). The vrecord script itself should run
under MSYS2 bash with path adjustments (`/c/` vs `/usr/local/`).

### 3. Full native rewrite (high effort)
Rewrite the GUI as a native Windows application (WinForms, WPF, or Qt)
with the capture logic as a thin layer over ffmpeg.exe with the DeckLink
SDK. This is effectively a new project. The `installer.nsi` skeleton in
the workflow targets this eventual form.

## What the CI workflow does today

- Builds a **portable zip** (scripts + Resources + a NOT-WINDOWS-NATIVE
  readme) so the artifact exists and the pipeline is green
- Generates an **NSIS installer skeleton** (`installer.nsi`) that a future
  port can fill in with real binaries
- Emits clear warnings that this is scaffolding, not a working Windows build
