#pragma once

// Option values shared with the vrecord shell backend.
//
// The Qt frontend writes ~/.vrecord.conf and then runs `vrecord -r|-p|-a`, and
// the shell validates every *_CHOICE against its own option arrays
// (_review_option): a value that is not in those lists aborts the run with
// exit 2 before ffmpeg/ffplay are ever started. These lists therefore MUST
// match vrecord's VIDEO_INPUT_OPTIONS, AUDIO_INPUT_OPTIONS, ... exactly
// (see the "list of selections for each vrecord option" block in ./vrecord).

#include <QMap>
#include <QString>
#include <QStringList>

namespace VrecordOptions {

inline QStringList videoInputs() {
    return {QStringLiteral("SDI"), QStringLiteral("HDMI"), QStringLiteral("Composite"),
            QStringLiteral("Component"), QStringLiteral("S-Video")};
}

inline QStringList audioInputs() {
    return {QStringLiteral("Analog"), QStringLiteral("SDI Embedded Audio"), QStringLiteral("Digital Audio (AES/EBU)")};
}

inline QStringList containers() {
    return {QStringLiteral("QuickTime"), QStringLiteral("Matroska"), QStringLiteral("AVI"), QStringLiteral("MXF"), QStringLiteral("MP4")};
}

inline QStringList dvContainers() {
    return {QStringLiteral("DV"), QStringLiteral("Matroska"), QStringLiteral("QuickTime")};
}

inline QStringList videoCodecs() {
    return {QStringLiteral("Uncompressed Video"), QStringLiteral("FFV1 version 3"), QStringLiteral("JPEG2000"),
            QStringLiteral("ProRes"), QStringLiteral("ProRes (HQ)"), QStringLiteral("h264"), QStringLiteral("HuffYUV")};
}

inline QStringList audioCodecs() {
    return {QStringLiteral("24-bit PCM"), QStringLiteral("24-bit FLAC"), QStringLiteral("AAC")};
}

inline QStringList audioChannels() {
    return {QStringLiteral("Mono"), QStringLiteral("Stereo")};
}

inline QStringList audioSampleRates() {
    return {QStringLiteral("96 kHz"), QStringLiteral("48 kHz"), QStringLiteral("44.1 kHz")};
}

inline QStringList bitDepths() {
    return {QStringLiteral("10 bit"), QStringLiteral("8 bit")};
}

inline QStringList aspectRatios() {
    return {QStringLiteral("4/3"), QStringLiteral("16/9")};
}

inline QStringList ntsc43Sars() { return {QStringLiteral("10/11"), QStringLiteral("8/9"), QStringLiteral("9/10"), QStringLiteral("4320/4739")}; }
inline QStringList ntsc169Sars() { return {QStringLiteral("40/33"), QStringLiteral("32/27"), QStringLiteral("6/5"), QStringLiteral("5760/4739")}; }
inline QStringList pal43Sars() { return {QStringLiteral("12/11"), QStringLiteral("16/15"), QStringLiteral("128/117")}; }
inline QStringList pal169Sars() { return {QStringLiteral("16/11"), QStringLiteral("64/45"), QStringLiteral("512/351")}; }

inline QStringList channelMappings() {
    return {QStringLiteral("2 Stereo Tracks (Channels 1 & 2 -> 1st Track Stereo, Channels 3 & 4 -> 2nd Track Stereo)"),
            QStringLiteral("1 Stereo Track (From Channels 1 & 2)"),
            QStringLiteral("1 Stereo Track (From Channels 3 & 4)"),
            QStringLiteral("Channel 1 -> 1st Track Mono, Channel 2 -> 2nd Track Mono"),
            QStringLiteral("Channel 2 -> 1st Track Mono, Channel 1 -> 2nd Track Mono"),
            QStringLiteral("Channel 1 -> Single Track Mono"),
            QStringLiteral("Channel 2 -> Single Track Mono")};
}

inline QStringList standards() {
    return {QStringLiteral("NTSC"), QStringLiteral("PAL"),
            QStringLiteral("23ps - 1080p23.98"), QStringLiteral("24ps - 1080p24"), QStringLiteral("Hp25 - 1080p25"),
            QStringLiteral("Hp29 - 1080p29.97"), QStringLiteral("Hp30 - 1080p30"), QStringLiteral("Hi50 - 1080i50"),
            QStringLiteral("Hi59 - 1080i59.94"), QStringLiteral("Hi60 - 1080i60"), QStringLiteral("Hp50 - 1080p50"),
            QStringLiteral("Hp59 - 1080p59.94"), QStringLiteral("Hp60 - 1080p60"), QStringLiteral("hp50 - 720p50"),
            QStringLiteral("hp59 - 720p59.94"), QStringLiteral("hp60 - 720p60"),
            QStringLiteral("2d23 - 2048p23.98"), QStringLiteral("2d24 - 2048p24"), QStringLiteral("2d25 - 2048p25"),
            QStringLiteral("2d29 - 2048p29.97"), QStringLiteral("2d30 - 2048p30"), QStringLiteral("2d50 - 2048p50"),
            QStringLiteral("2d59 - 2048p59.94"), QStringLiteral("2d60 - 2048p60"),
            QStringLiteral("4k23 - UHD 23.98"), QStringLiteral("4k24 - UHD 24"), QStringLiteral("4k25 - UHD 25"),
            QStringLiteral("4k29 - UHD 29.97"), QStringLiteral("4k30 - UHD 30"), QStringLiteral("4k50 - UHD 50"),
            QStringLiteral("4k59 - UHD 59.94"), QStringLiteral("4k60 - UHD 60"),
            QStringLiteral("4d23 - 4K DCI 23.98"), QStringLiteral("4d24 - 4K DCI 24"), QStringLiteral("4d25 - 4K DCI 25"),
            QStringLiteral("4d29 - 4K DCI 29.97"), QStringLiteral("4d30 - 4K DCI 30"), QStringLiteral("4d50 - 4K DCI 50"),
            QStringLiteral("4d59 - 4K DCI 59.94"), QStringLiteral("4d60 - 4K DCI 60")};
}

inline QStringList embedLogs() { return {QStringLiteral("Yes"), QStringLiteral("No")}; }

inline QStringList playbackViews() {
    return {QStringLiteral("Unfiltered"), QStringLiteral("Visual"), QStringLiteral("Audio + Video"),
            QStringLiteral("Visual + Numerical"), QStringLiteral("Color Matrix"), QStringLiteral("Bit Planes"),
            QStringLiteral("Frame Positioning"), QStringLiteral("Captions"), QStringLiteral("Quality Control View (mpv)")};
}

inline QStringList waveformScales() { return {QStringLiteral("digital"), QStringLiteral("millivolts"), QStringLiteral("ire")}; }
inline QStringList signalViews() { return {QStringLiteral("Broadcast Range"), QStringLiteral("Full Range")}; }
inline QStringList signalInterlacements() { return {QStringLiteral("auto"), QStringLiteral("bff"), QStringLiteral("tff"), QStringLiteral("prog")}; }
inline QStringList monitorAudio() { return {QStringLiteral("Yes"), QStringLiteral("No")}; }
inline QStringList themes() { return {QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")}; }

// Values written by earlier Qt builds that the shell rejects, mapped to the
// shell spelling so an existing ~/.vrecord.conf keeps working.
inline QString migrate(const QString& key, const QString& value) {
    static const QMap<QString, QMap<QString, QString>> table{
        {QStringLiteral("VIDEO_INPUT_CHOICE"),
         {{QStringLiteral("sdi"), QStringLiteral("SDI")}, {QStringLiteral("hdmi"), QStringLiteral("HDMI")},
          {QStringLiteral("component"), QStringLiteral("Component")}, {QStringLiteral("composite"), QStringLiteral("Composite")},
          {QStringLiteral("s-video"), QStringLiteral("S-Video")}, {QStringLiteral("s_video"), QStringLiteral("S-Video")},
          {QStringLiteral("optical sdi"), QStringLiteral("SDI")}}},
        {QStringLiteral("AUDIO_INPUT_CHOICE"),
         {{QStringLiteral("embedded"), QStringLiteral("SDI Embedded Audio")}, {QStringLiteral("hdmi"), QStringLiteral("SDI Embedded Audio")},
          {QStringLiteral("aesebu"), QStringLiteral("Digital Audio (AES/EBU)")}, {QStringLiteral("analog"), QStringLiteral("Analog")}}},
        {QStringLiteral("AUDIO_MAPPING_CHOICE"),
         {{QStringLiteral("2 channels"), QStringLiteral("1 Stereo Track (From Channels 1 & 2)")},
          {QStringLiteral("4 channels"), QStringLiteral("2 Stereo Tracks (Channels 1 & 2 -> 1st Track Stereo, Channels 3 & 4 -> 2nd Track Stereo)")},
          {QStringLiteral("8 channels"), QStringLiteral("2 Stereo Tracks (Channels 1 & 2 -> 1st Track Stereo, Channels 3 & 4 -> 2nd Track Stereo)")},
          {QStringLiteral("16 channels"), QStringLiteral("2 Stereo Tracks (Channels 1 & 2 -> 1st Track Stereo, Channels 3 & 4 -> 2nd Track Stereo)")}}},
        {QStringLiteral("EMBED_LOGS_CHOICE"), {{QStringLiteral("yes"), QStringLiteral("Yes")}, {QStringLiteral("no"), QStringLiteral("No")}}},
        {QStringLiteral("MONITOR_AUDIO_CHOICE"), {{QStringLiteral("yes"), QStringLiteral("Yes")}, {QStringLiteral("no"), QStringLiteral("No")}}},
        {QStringLiteral("PLAYBACKVIEW_CHOICE"),
         {{QStringLiteral("none"), QStringLiteral("Visual")}, {QStringLiteral("ffplay"), QStringLiteral("Visual")},
          {QStringLiteral("system"), QStringLiteral("Visual")}, {QStringLiteral("mpv"), QStringLiteral("Quality Control View (mpv)")}}},
        {QStringLiteral("PLAYBACKVIEW_CHOICE_PASS"),
         {{QStringLiteral("none"), QStringLiteral("Visual")}, {QStringLiteral("ffplay"), QStringLiteral("Visual")},
          {QStringLiteral("system"), QStringLiteral("Visual")}, {QStringLiteral("mpv"), QStringLiteral("Quality Control View (mpv)")}}},
        {QStringLiteral("WAVEFORM_SCALE_CHOICE"),
         {{QStringLiteral("IRE"), QStringLiteral("ire")}, {QStringLiteral("mV"), QStringLiteral("millivolts")}, {QStringLiteral("Digital"), QStringLiteral("digital")}}},
        {QStringLiteral("SIGNAL_VIEW_CHOICE"),
         {{QStringLiteral("Color"), QStringLiteral("Broadcast Range")}, {QStringLiteral("Luma"), QStringLiteral("Broadcast Range")},
          {QStringLiteral("Waveform"), QStringLiteral("Broadcast Range")}, {QStringLiteral("Vectorscope"), QStringLiteral("Broadcast Range")}}},
        {QStringLiteral("SIGNAL_INT_CHOICE"),
         {{QStringLiteral("Auto"), QStringLiteral("auto")}, {QStringLiteral("Interlaced"), QStringLiteral("auto")}, {QStringLiteral("Progressive"), QStringLiteral("prog")}}},
        {QStringLiteral("NTSC_43_SAR_CHOICE"), {{QStringLiteral("8:9"), QStringLiteral("8/9")}, {QStringLiteral("10:11"), QStringLiteral("10/11")}}},
        {QStringLiteral("NTSC_169_SAR_CHOICE"), {{QStringLiteral("32:27"), QStringLiteral("32/27")}, {QStringLiteral("40:33"), QStringLiteral("40/33")}}},
        {QStringLiteral("PAL_43_SAR_CHOICE"), {{QStringLiteral("16:15"), QStringLiteral("16/15")}, {QStringLiteral("12:11"), QStringLiteral("12/11")}}},
        {QStringLiteral("PAL_169_SAR_CHOICE"), {{QStringLiteral("64:45"), QStringLiteral("64/45")}, {QStringLiteral("16:11"), QStringLiteral("16/11")}}},
        {QStringLiteral("DV_CONTAINER_CHOICE"),
         {{QStringLiteral("dv"), QStringLiteral("DV")}, {QStringLiteral("mov"), QStringLiteral("QuickTime")}, {QStringLiteral("mkv"), QStringLiteral("Matroska")}}},
        {QStringLiteral("AUDIO_CHANNEL_CHOICE"),
         {{QStringLiteral("1"), QStringLiteral("Mono")}, {QStringLiteral("2"), QStringLiteral("Stereo")},
          {QStringLiteral("4"), QStringLiteral("Stereo")}, {QStringLiteral("8"), QStringLiteral("Stereo")}}},
        {QStringLiteral("AUDIO_MODE_SR_CHOICE"),
         {{QStringLiteral("96000"), QStringLiteral("96 kHz")}, {QStringLiteral("48000"), QStringLiteral("48 kHz")},
          {QStringLiteral("44100"), QStringLiteral("44.1 kHz")}, {QStringLiteral("32000"), QStringLiteral("48 kHz")}}},
    };
    const auto keyIt = table.constFind(key);
    if (keyIt == table.constEnd()) {
        return value;
    }
    return keyIt->value(value, value);
}

// ffmpeg decklink -video_input / -audio_input names (vrecord _lookup_choice)
inline QString ffmpegVideoInput(const QString& choice) {
    if (choice == QStringLiteral("HDMI")) return QStringLiteral("hdmi");
    if (choice == QStringLiteral("Composite")) return QStringLiteral("composite");
    if (choice == QStringLiteral("Component")) return QStringLiteral("component");
    if (choice == QStringLiteral("S-Video")) return QStringLiteral("s_video");
    return QStringLiteral("sdi");
}

inline QString ffmpegAudioInput(const QString& choice) {
    if (choice == QStringLiteral("SDI Embedded Audio")) return QStringLiteral("embedded");
    if (choice == QStringLiteral("Digital Audio (AES/EBU)")) return QStringLiteral("aes_ebu");
    return QStringLiteral("analog");
}

inline bool isHdStandard(const QString& standard) {
    return !standard.isEmpty() && standard != QStringLiteral("NTSC") && standard != QStringLiteral("PAL");
}

} // namespace VrecordOptions
