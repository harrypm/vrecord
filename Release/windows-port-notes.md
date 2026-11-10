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

---

# Qt6 frontend (qt6/) - status 2026-11-10

Commits on `windows-port`: c14ab10 (feeds, shell-backed record, card-clocked playout, v210 relay) and dd3600f (standard follows input, output standard, native preview framing). Verified on a Blackmagic Intensity Pro 4K (Desktop Video 16.0, SDK headers 16.0) with a 1080p50 HDMI camera on the input and an HDMI monitor on the output.

## Architecture
- **Live feeds** (`FrameFeed` + `VideoView`): one ffmpeg process per feed writes raw BGRA frames to a pipe (`-f rawvideo -flush_packets 1 pipe:1`). Each frame repaints its pane, so there is no timer, no fps filter and no JPEG polling. The same process drives the card, so the output pane shows exactly the frames on the wire, held back by the 0.5 s DeckLink preroll. Launches and outcomes are logged to `VrecordQt6-feeds.log` in the package root.
- **Record / passthrough / audio check** run the real `vrecord -r|-p|-a` shell backend through MSYS2 bash, with the same environment the GTK launcher uses. ffmpeg encodes and ffplay opens as the monitor. The GUI releases the card first. Stop posts WM_CLOSE to the `mode:*` player window so the tee muxer writes the trailer; `taskkill /FI "WINDOWTITLE eq mode:*"` never matched.
- **Config**: values the shell reads must use its exact option spellings (`VrecordOptions.h` mirrors the `*_OPTIONS` arrays; anything else aborts with exit 2 in `_review_option`). Legacy values are migrated on load. Settings autosave about 0.7 s after a change. `QT6_CONFIG_REV` gates one-time migrations. Only the GTK frontend's `_writesettings` rewrites the file and drops unknown keys.
- **Standard follows the input**: the mode the driver detects is applied to `STANDARD_CHOICE`, with `HD_CHOICE` derived when saving. This happens while the preview runs (on first frame, then a 2 s poll) and before a shell session starts. A mismatched standard captures blank "No input signal" frames: luma 64 in 10-bit, an empty recording and a black ffplay monitor.
- **Output standard** (`OUTPUT_STANDARD_CHOICE`, separate from the capture Standard) applies to file playout and the test signal:
  - **"Auto (match file)"** plays the file's mode if the card lists it (`IDeckLinkOutput::GetDisplayModeIterator`). Otherwise it keeps the file's frame rate on the tallest raster that isn't an upscale. Example: 4K50 HEVC becomes Hp50 on this card, which only outputs UHD up to 30p.
  - **Composite/S-Video** outputs are SD only: PAL for 25/50 fps, NTSC otherwise.
  - **Relay on**: the output follows the input and the combo is disabled.
  - Auto also drives the shell's `FILEPLAY_AUTO_CHOICE`.
- **Native preview framing**: the input pane is sized from the detected mode (960x540 for 1080, 648x486 for NTSC 4:3, using the Display Aspect choice for SD). It relaunches when the live mode differs from the one it was sized for.

## Findings
- **No `-re` on DeckLink playout.** The muxer blocks once the card's buffer is full, so the card clock paces it. With `-re` as a second clock, 4K50 HEVC reached only about 34 fps on the wire; without it, a steady 50. (Offline, decode + scale + v210 runs at more than 3x real time, so the pipeline was never the limit.) The playout graph converts to `yuv422p10le` once, then splits for the card (v210) and the pane.
- **Relay must re-pack, not copy**: capture with `-raw_format yuv422p10` (v210 on the wire) and encode `-c:v v210`. With `-c:v copy` the stream keeps the capture's 1/1000000 time base and the muxer fails with "Unsupported video size, framerate or field order". Re-packing stays 10-bit 4:2:2 and holds a steady 50 fps.
- **Intensity Pro 4K quirks**:
  - `bmdDeckLinkStatusVideoInputSignalLocked` reads NO even mid-capture, while `DetectedVideoInputMode` is live. Detection therefore uses the mode without the lock flag (`allowStale`). Idle, the driver keeps the last detected mode.
  - `HDMIOutputActualMode` and `ReceivedEDID` return E_NOTIMPL, so the software cannot see whether the HDMI sink is connected.
  - The output connection is fixed at 0x32 (HDMI + Composite + S-Video); SetInt is accepted but ignored.
  - The card reports full duplex (`dxfu`), so capture and relay to the same card work.
- **"No output on hardware" was the HDMI link, not software.** The driver reported normal playout (busy=2, correct mode, `012v` pixel format) while the monitor showed "no signal". Direct SDK output (`vsignal sdktest`) and Resolve were dark too. A cable replug restored the handshake, after which ffmpeg, SDK, relay and file playout all displayed. If the output goes dark again, replug the cable before debugging.
- ffmpeg 9.0.1 `decklink_enc` uses the SDK 14.2.1 compat ABI (`IDeckLinkOutput_v14_2_1`, its own frame class). It works with driver 16.0.
- Startup bug (fixed): textChanged handlers synced the whole UI into the config while `populateFromConfig()` was still filling widgets, and the self-test ran on every launch. Together they wiped `DIR`/`ID` and leaked `selftest-play.mkv`. Syncs are now suppressed while populating, the self-test runs only with `--self-test`, and it checks that every setting survives.
- Shell fixes: `Hp50`/`Hp59`/`Hp60` were missing from the aspect tables ("the standard wasn't set to an expected..."), and `_frames_to_hhmmss` printed `invalid number` for times under 1 s because bc prints `.033`.

## Build and test
- Build and deploy: `qt6/build/agent-build.cmd` (MSYS2 UCRT64, Ninja). It copies `VrecordQt6.exe`, `vsignal.exe`, `VrecordOutputTest.exe` and `vrecord` into `Release/artifacts/vrecord_windows_2026-03-03/`.
- `VrecordQt6.exe --self-test` runs 18 checks on a throwaway config and writes `VrecordQt6-selftest.log`.
- `qt6/tests/backend-record-test.sh -r <config>` runs the shell backend headless (`VRECORD_HEADLESS=1`, player replaced by `cat`) for machine checks.
- `vsignal probe`: driver state (connections, pass-through mode, detected input, output mode, pixel format, busy).
- `vsignal sdktest [MODE] [SECONDS]`: colour bars straight through SDK 16 with driver-allocated frames, to tell card or cabling problems from ffmpeg ones.
- `vsignal setout <mask>`: write and read back the output connection.

## Known gaps
- FFV1 10-bit at 1080p50 encodes slower than real time on the test PC. A 10 s capture queued about 5 GB of frames in RAM before draining, so long HD FFV1 captures need a faster CPU or a lighter codec.
- The shell's `_review_option` and STANDARD_OPTIONS still only accept HD standards with `HD_CHOICE=true`; the Qt frontend sets it on save.
