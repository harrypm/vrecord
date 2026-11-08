# vrecord Windows Port Notes

Status (2026-11-08): **GUI verified working in autotest** (no freezes across idle + scripted clicks), dark theme, native dropdowns, all optional tools embedded.

## Root causes fixed this session (all verified with instrumented builds)
1. **Constant freeze**: gtkdialog timers with disabled="true" only make the widget insensitive - their actions STILL fire. preview_status_timer spawned bash every 300ms, each a g_spawn_sync blocking the GTK main loop 40-185ms. Fix: delete timer blocks entirely on Windows (awk in the GUI hardening block).
2. **Broken option lists**: items containing raw & (e.g. "1 Stereo Track (From Channels 1 & 2)") broke the gtkdialog XML parser mid-dialog; every list after the audio-mapping list rendered empty. Fix: XML-escape & at emit time in _expand_list2items.
3. **"Column 0" headers**: gtkdialog's list widget defaults the column label to "Column 0". Windows now uses <comboboxtext> native dropdowns instead of tree lists (also fixes selection unreliability in the Win32 build).
4. **Window opened 1446x1381**: gtkdialog --geometry used gtk_widget_set_size_request (minimum only, content natural size wins). Patched widget_window.c to use gtk_window_set_default_size + demote CENTER_ALWAYS to CENTER_ONCE (CENTER_ALWAYS fights user resizing on Win32).
5. **Launcher**: record.cmd had cmd.exe quoting fragility ("or was unexpected at this time"). Replaced with a native record.exe launcher (C, CreateProcess -> MSYS2 bash, no cmd.exe).

## Embedded optional tools (all verified running from portable bin)
- mpv v0.42.0 (shinchiro build)
- mediaconch 25.04 (MediaArea CLI)
- dvrescue 24.07 + dvpackager script (MIPoPS/MediaArea)
- gnuplot 6.0.3 (MSYS2 UCRT64 build) + share/gnuplot
- mkvpropedit/mkvmerge 103.0 (mkvtoolnix)
- deckcontrol: **built from source** (bavc/deckcontrol) against the DeckLink SDK Windows headers with a COM shim (DeckLinkAPIDispatch_win.cpp = CoCreateInstance replacement for the Linux dlopen dispatch)

## Build trees / new files
- Release/vrecord-launcher.c - native launcher
- Release/autotest-gui.ps1 - click autotest with Responding sampling
- Release/autotest-visual.ps1 - navigates tabs + screenshots + freeze diagnostics
- gtkdialog patches in record-build/patch_gtkdialog_*.py (bash spawn, popen, instrument, window size)
- Package: Release/artifacts/vrecord_windows_2026-11-05/ + record_2026-11-08_windows_portable.zip (132.6 MB)

## Known gaps
- Deck Control UI works via deckcontrol.exe but the status/timecode auto-refresh timer is removed on Windows (manual Refresh); RS422 needs a serial deck anyway.
- Preview page is stubbed on Windows (use Passthrough for live view).
- mpv download was vendored from shinchiro 20261108 after GitHub CDN 504s; re-pin version when scripting builds.
- Window width clamps to ~1383px (long non-wrapping text labels set the GTK minimum); fits 1440p, tight on 1080p.
- NSIS installer still needs makensis.
