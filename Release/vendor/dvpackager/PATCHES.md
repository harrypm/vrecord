# dvpackager (vendored)

`dvpackager` is part of the DVRescue project (Moving Image Preservation of
Puget Sound, BSD 3-Clause, see `LICENSE.txt`). vrecord calls it after a DV
capture to split / rewrap the stream (DV tab: *dvpackager options*).

- `dvpackager.orig`: the upstream script as obtained (md5
  `99e984c3ad6daac803b2d1406a7f4c8c`).
- `dvpackager`: the copy the Windows package ships in `bin/`, with the
  changes below. Every change is marked `vrecord patch` in the script.

## Changes

1. **bc stand-in.** MSYS2 and Git for Windows have no `bc`, and the script
   pipes its arithmetic and comparisons to `bc [-l]`. When `bc` is missing,
   a `bc` shell function evaluates each expression with awk. It handles
   `scale=N;` prefixes, `if (cond) a else b`, and comparisons that give 1/0.
   It strips leading zeros from integers first, because bc reads `033` as 33
   and awk would read it as octal. When a real `bc` exists, nothing changes.
2. **Native temp paths.** `_maketemp` gives `/tmp/...` paths, which the script
   passes to native Windows tools (dvrescue XML/VTT/SCC output, ffmpeg chapter
   metadata). Under MSYS/MinGW/Cygwin they are converted with `cygpath -m`.
3. **MOV frame rate.** ffmpeg's raw DV demuxer reports `avg_frame_rate`
   60000/1. A MOV's timecode (tmcd) track then fails with
   `fps 60000 is too large` and the file has no readable header. This was
   seen with ffmpeg 9.0.1, so it is not Windows-only. MOV outputs now get
   `-r <segment rate>`, taken from the dvrescue XML.

vrecord itself passes `-F/-M/-D/-X` (its ffmpeg-ma, mediainfo, dvrescue and
xmlstarlet), because dvpackager otherwise looks for a plain `ffmpeg` or a
DVRescue GUI install.

Tested on Windows with a synthetic 5 s PAL DV file: rewrap to Matroska, and to
QuickTime with `-s -d -t`. Both give 125/125 frames, dvvideo + pcm_s16le,
and the MOV has a valid tmcd track.
