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
- **Preview and output together** (full-duplex cards): the input preview keeps running during file playout and the test signal. If the preview is relaying, it relaunches without the relay before playout claims the output, and gets the relay back when the user stops the output. Half-duplex cards still stop the preview. While relaying, the output pane mirrors the input frames.
- **Feed panes** (`VideoView`) take the feed's aspect (height-for-width, re-evaluated when the raster changes) and paint only the picture area black. Leftover layout space stays window-coloured instead of showing black borders. Frames still queued from a stopped feed are dropped so a cleared pane stays cleared. The Activity log and playout notes live on the Logs tab.
- **Frame-exact transport**: positions are frame indices at the playout rate.
  - **Pause** replaces the playout with a *hold* process (`trim=end_frame=1,loop=-1:size=1,setpts`, silent audio, `-r` at the playout rate) that repeats exactly the frame on the wire. Suspending ffmpeg instead let the card play out its queued frames and stop past the frame on screen.
  - **Step** while paused moves the hold (card and pane); **resume** continues from the next frame.
  - **Seeking** to frame N uses `-ss (N-0.5)/fps`, so ffmpeg's accurate seek starts exactly on N. This was verified bit-exact against `select=eq(n,N)` on several frames.
  - **Relay re-enabled** while a file is loaded halts the file on its exact frame and gives the relay the output; Play resumes the file from that frame.
- **Pane delay = muxer queue depth**: ffmpeg's decklink muxer schedules up to 2 x preroll frames ("frames buffer size") and blocks when that is full, so a frame reaches the wire 2 x preroll after ffmpeg emits it. Delaying the pane by only 1 x preroll made the pane and counter run ahead of the wire. Playout now passes `-preroll 0.25` (lower latency; 4K50 HEVC still holds a steady 50 fps) and delays the pane by 2 x preroll.
- **Transport counter**: fixed-format monospace `HH:MM:SS:FF / HH:MM:SS:FF   frame N`, with the state (PLAYING / PAUSED / CUED / HALTED - RELAY ON OUTPUT / STOPPED) in a separate fixed-width tag, so nothing shifts on play/pause.
- **Video Preview is the single output page** (the Output tab is gone): output and relay devices side by side, the *Test signal output* tick beside the relay tick, a one-row *Signal / audio generator* (compact standard, pattern, audio), the output picture with its meter, the transport and the counter. Activity, preview/playout and test-signal notes are on the Logs tab. The input meter is two bars wide (pair-only mode: it shows the selected monitor pair, labelled e.g. 3/4); the output meter carries the played/relayed pair.
- **Counter**: an editable monospace timecode (`HH:MM:SS:FF` + Enter jumps) and an editable frame number. Playing keeps playing from there, paused stays paused on that frame, and cued/halted moves the cue.
- **Audio meters** (`LevelMeter`): vertical, beside the input and output pictures, laid out in stereo pairs (1/2 ... 15/16).
  - Levels come from ffmpeg itself: `asetnsamples=2400,astats=...,ametadata=mode=print:direct=1:file='pipe\:2'` on stderr. `direct=1` is essential, because without it the blocks arrive seconds late in one burst. FrameFeed parses them out of stderr.
  - Input levels come from the preview capture (it now also captures audio, same input and channel count as a recording). Output levels come from the playout / hold / test-signal process.
  - **Monitor pair** (`MONITOR_PAIR_CHOICE`, default 1/2): highlighted on the meters and sent to the relay output (`pan=stereo|c0=cN|c1=cN+1`, previously a downmix of every channel).
- **Decklink page audio**: Monitor Audio (moved from Config), capture channels 8/16 (`DECKLINK_AUDIO_CHANNELS_CHOICE`, passed to the shell's `-channels`), and a readout of the fixed 48 kHz DeckLink capture rate.
- **ffplay monitor checkbox** (`FFPLAY_MONITOR_CHOICE`): ticked runs Record/Passthrough with the ffplay window and the selected View/scopes, as GTK does. Unticked shows the session in the Video Preview pane instead:
  - The shell's player becomes an ffmpeg that writes BGRA frames to a named pipe the GUI serves (`PipeFeed`, `VRECORD_MONITOR_PIPE` = `//./pipe/vrecord-monitor-<pid>`, `VRECORD_MONITOR_SIZE`).
  - Levels go to a small file the GUI tails (`VRECORD_MONITOR_LEVELS`). `ametadata` cannot open a pipe path, hence the file.
  - Stop closes the GUI's pipe end, which ends the capture exactly like closing ffplay; the file is finalised. Verified with `vsignal monitortest` (timed and stopped).
- **Test signal**: the card mode comes from the modes the card lists. A locked HD/UHD pattern keeps its raster at the highest same-family rate the card has (PQ 2160p at 50 Hz → 4k25 on an Intensity Pro 4K, where it used to exit with "nothing was written"). There is no `-re`, a single scale feeds both card and pane, and the real ffmpeg error is shown.
- **ffplay monitor: start-up, shape, view switching**: passthrough took 5-7 s to open. About 30 `_maketemp` calls each started mktemp + basename + uname + cygpath (about 0.15 s per process in MSYS2); they now create files in pure bash (noclobber) with the native /tmp path resolved once. The DeckLink grab skips the default probe (`-probesize 32 -analyzeduration 0`), and the GUI's once-per-session orphan sweep runs detached, sparing its own children. The ffplay window now appears about 2 s after pressing Passthrough. View shape: *View shape (monitor)* on the Decklink page is 16:9, 21:9 (43:18 for 3440x1440) or 5:4 (`FFPLAY_VIEW_ASPECT_CHOICE` -> `VRECORD_VIEW_ASPECT`). The Visual view widens its range panel so the composite is exactly that shape, and other views are padded at true geometry (`_fit_view`), so fullscreen fills the monitor. The window opens at 2/3 of the smallest monitor's height (`VRECORD_FFPLAY_HEIGHT`, DPI-corrected). Views: every video view is passed as its own `-vf` (selected first), so **w** in the window cycles them live. Stock ffplay has no 1-9 view keys (9/0 are volume), which would need a patched build. Starting a session unticks *Test signal output* (the session took the card).
- **ffplay monitor (record / passthrough)**: the bundled `ffplay-ma` / `ffmpeg-ma` have no `drawtext` (no freetype). The passthrough *Visual* view burns the timecode in with drawtext, so ffplay rejected the whole graph ("Filter not found") and passthrough ended within about 5 s. `_setup_vrecord_process` now swaps every `drawtext=...` for `null` when the player lacks it, so the scopes still show without text labels. On Windows the window opens 720 px high (`VRECORD_FFPLAY_HEIGHT`) with only `-y` set: ffplay derives the width from the view's aspect, so the window fits the picture exactly (`-x` and `-y` together force a box and letterbox it). `bc` is not in MSYS2, so the duration arithmetic uses awk. `vsignal monitortest <dir> [stop] [ffplay] [pass]` covers all four session variants.
- **Test signal start no longer freezes the GUI**: the GUI path used to run on the UI thread, with 400 ms + 1.5 s `msleep`s (doubled by the teardown-race retry) plus a PowerShell orphan sweep on every stop, for 2 to 7 s of frozen window. It now returns at once: settle -> connector + launch -> verify (and one retry) run off `QTimer::singleShot` steps, and the outcome arrives as `testSignalResult(ok, message)` (failure unticks the box and shows the error). A generation counter bumped by every stop cancels pending steps. The orphan sweep runs once per GUI session (the GUI's feeds are its children); the CLI keeps it on every stop. `vsignal signaltest` now reports `ui-block` (about 0.1 s) and fails over 1 s.
- **Drag and drop**: a folder (or a file, whose folder is used) onto the recording / auxiliary folder boxes; a file onto the playout-file box or the output picture. Anywhere else in the window: a folder sets the recording directory, a file the playout file. A drop a specific box cannot take (a folder on the playout box) is refused there with the no-drop cursor - Qt propagates unaccepted drag events to parents, so the filter accepts with `Qt::IgnoreAction` to stop it reaching the window fallback. Covered by the self-test (19 checks) with synthetic QDragEnter/QDrop events.
- **Output connector persistence**: rebuilding the connector list from the hardware used to drop the saved choice back to Auto. It now restores it, like the input connector.
- **Drag and drop**: folders onto the recording / auxiliary folder boxes (a dropped file uses its folder); media files onto the playout box or straight onto the output picture.
- **fps readout**: counts frames over the last second instead of averaging 1/dt, which overstated bursty feeds (70 "fps" on a 50p input).
- **Icon**: `qt6/resources/vrecord.ico` (the GTK launcher's icon) is embedded as the exe icon (`vrecord.rc`, also for vsignal) and set as the window icon (`vrecord.qrc`).
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
- `vsignal signaltest`: the GUI's test signal across SD/HD/UHD patterns; reports the mode the card actually outputs.
- `vsignal monitortest <OUT_DIR> [stop]`: records 8 s (or until Stop with `stop`) with the ffplay window off, and checks that frames and levels reach the GUI and the recording completes.
- `vsignal transporttest [FILE]`: on the card, with the GUI's backend code and a throwaway config, checks relay → playout alongside preview → pause holds the on-wire frame → step +1 → resume → relay halts the file → Play resumes from the halted frame. Close the GUI first, since it holds the card.

## Known gaps
- FFV1 10-bit at 1080p50 encodes slower than real time on the test PC. A 10 s capture queued about 5 GB of frames in RAM before draining, so long HD FFV1 captures need a faster CPU or a lighter codec.
- The shell's `_review_option` and STANDARD_OPTIONS still only accept HD standards with `HD_CHOICE=true`; the Qt frontend sets it on save.
