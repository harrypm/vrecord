#pragma once

// DeckLink display modes for the vrecord standards (mirrors the STANDARD_CHOICE
// tables in Resources/vrecord_functions: _start_video_preview and
// _start_file_playback).

#include <QString>
#include <QtGlobal>

struct VideoMode {
    QString formatCode;   // ffmpeg decklink -format_code
    int width = 0;
    int height = 0;
    int rateNum = 0;      // frame rate (frames, not fields)
    int rateDen = 1;
    QString fieldOrder;   // "tff", "bff" or empty for progressive

    bool isValid() const { return width > 0 && height > 0 && rateNum > 0; }
    double fps() const { return rateDen > 0 ? double(rateNum) / rateDen : 0.0; }
    QString rate() const { return QStringLiteral("%1/%2").arg(rateNum).arg(rateDen); }
    QString size() const { return QStringLiteral("%1x%2").arg(width).arg(height); }
};

namespace VideoModes {

inline void rateFromSuffix(const QString& code, int* num, int* den) {
    const QString r = code.right(2);
    *den = 1;
    if (r == QStringLiteral("23")) { *num = 24000; *den = 1001; }
    else if (r == QStringLiteral("24")) { *num = 24; }
    else if (r == QStringLiteral("25")) { *num = 25; }
    else if (r == QStringLiteral("29")) { *num = 30000; *den = 1001; }
    else if (r == QStringLiteral("30")) { *num = 30; }
    else if (r == QStringLiteral("50")) { *num = 50; }
    else if (r == QStringLiteral("59")) { *num = 60000; *den = 1001; }
    else if (r == QStringLiteral("60")) { *num = 60; }
    else { *num = 0; }
}

// "Hp29 - 1080p29.97" -> {Hp29, 1920x1080, 30000/1001, progressive}
inline VideoMode forStandard(const QString& standard) {
    VideoMode m;
    if (standard == QStringLiteral("NTSC")) {
        return {QStringLiteral("ntsc"), 720, 486, 30000, 1001, QStringLiteral("bff")};
    }
    if (standard == QStringLiteral("PAL")) {
        return {QStringLiteral("pal"), 720, 576, 25, 1, QStringLiteral("tff")};
    }
    const QString code = standard.left(4);
    if (code.size() != 4) {
        return m;
    }
    m.formatCode = code;
    const QString family = code.left(2);
    if (family == QStringLiteral("23") || family == QStringLiteral("24")) {
        // 23ps / 24ps
        m.width = 1920; m.height = 1080;
        if (code == QStringLiteral("23ps")) { m.rateNum = 24000; m.rateDen = 1001; } else { m.rateNum = 24; }
        return m;
    }
    if (family == QStringLiteral("Hp")) { m.width = 1920; m.height = 1080; }
    else if (family == QStringLiteral("Hi")) { m.width = 1920; m.height = 1080; m.fieldOrder = QStringLiteral("tff"); }
    else if (family == QStringLiteral("hp")) { m.width = 1280; m.height = 720; }
    else if (family == QStringLiteral("2d")) { m.width = 2048; m.height = 1080; }
    else if (family == QStringLiteral("4k")) { m.width = 3840; m.height = 2160; }
    else if (family == QStringLiteral("4d")) { m.width = 4096; m.height = 2160; }
    else { return VideoMode{}; }
    rateFromSuffix(code, &m.rateNum, &m.rateDen);
    if (!m.fieldOrder.isEmpty()) {
        // 1080i50/59.94/60: field rate in the name, frame rate is half
        if (m.rateNum == 50) { m.rateNum = 25; }
        else if (m.rateNum == 60000) { m.rateNum = 30000; }
        else if (m.rateNum == 60) { m.rateNum = 30; }
    }
    return m;
}

// broadcast size/rate combination -> decklink mode (mirrors the auto-detect
// table of _start_file_playback); empty formatCode when the file needs
// converting to the selected standard instead
inline VideoMode forNative(int width, int height, int rateNum, int rateDen, const QString& fieldOrder) {
    const bool interlaced = !fieldOrder.isEmpty();
    const QString rate = QStringLiteral("%1/%2").arg(rateNum).arg(rateDen);
    const auto make = [&](const QString& code, const QString& field) {
        return VideoMode{code, width, height, rateNum, rateDen, field};
    };
    if (width == 720 && (height == 486 || height == 480) && rate == QStringLiteral("30000/1001")) {
        VideoMode m = make(QStringLiteral("ntsc"), interlaced ? fieldOrder : QStringLiteral("bff"));
        m.height = 486;
        return m;
    }
    if (width == 720 && height == 576 && rate == QStringLiteral("25/1")) {
        return make(QStringLiteral("pal"), interlaced ? fieldOrder : QStringLiteral("tff"));
    }
    struct Entry { int w; int h; const char* rate; const char* prog; const char* inter; };
    static const Entry table[] = {
        {1920, 1080, "24000/1001", "23ps", nullptr}, {1920, 1080, "24/1", "24ps", nullptr},
        {1920, 1080, "25/1", "Hp25", "Hi50"}, {1920, 1080, "30000/1001", "Hp29", "Hi59"}, {1920, 1080, "30/1", "Hp30", "Hi60"},
        {1920, 1080, "50/1", "Hp50", nullptr}, {1920, 1080, "60000/1001", "Hp59", nullptr}, {1920, 1080, "60/1", "Hp60", nullptr},
        {1280, 720, "50/1", "hp50", nullptr}, {1280, 720, "60000/1001", "hp59", nullptr}, {1280, 720, "60/1", "hp60", nullptr},
        {2048, 1080, "24000/1001", "2d23", nullptr}, {2048, 1080, "24/1", "2d24", nullptr}, {2048, 1080, "25/1", "2d25", nullptr},
        {2048, 1080, "30000/1001", "2d29", nullptr}, {2048, 1080, "30/1", "2d30", nullptr}, {2048, 1080, "50/1", "2d50", nullptr},
        {2048, 1080, "60000/1001", "2d59", nullptr}, {2048, 1080, "60/1", "2d60", nullptr},
        {3840, 2160, "24000/1001", "4k23", nullptr}, {3840, 2160, "24/1", "4k24", nullptr}, {3840, 2160, "25/1", "4k25", nullptr},
        {3840, 2160, "30000/1001", "4k29", nullptr}, {3840, 2160, "30/1", "4k30", nullptr}, {3840, 2160, "50/1", "4k50", nullptr},
        {3840, 2160, "60000/1001", "4k59", nullptr}, {3840, 2160, "60/1", "4k60", nullptr},
        {4096, 2160, "24000/1001", "4d23", nullptr}, {4096, 2160, "24/1", "4d24", nullptr}, {4096, 2160, "25/1", "4d25", nullptr},
        {4096, 2160, "30000/1001", "4d29", nullptr}, {4096, 2160, "30/1", "4d30", nullptr}, {4096, 2160, "50/1", "4d50", nullptr},
        {4096, 2160, "60000/1001", "4d59", nullptr}, {4096, 2160, "60/1", "4d60", nullptr},
    };
    for (const Entry& e : table) {
        if (e.w == width && e.h == height && rate == QLatin1String(e.rate)) {
            if (interlaced && e.inter) {
                return make(QLatin1String(e.inter), fieldOrder);
            }
            return make(QLatin1String(e.prog), QString());
        }
    }
    return VideoMode{};
}

// ffmpeg format code / DeckLink fourCC ("ntsc", "pal", "Hp50", "4k30") -> mode
inline VideoMode forCode(const QString& code) {
    if (code == QStringLiteral("ntsc")) {
        return forStandard(QStringLiteral("NTSC"));
    }
    if (code == QStringLiteral("pal")) {
        return forStandard(QStringLiteral("PAL"));
    }
    return code.size() == 4 ? forStandard(code) : VideoMode{};
}

// display aspect of a standard: SD follows the Display Aspect Ratio choice
inline double displayAspect(const VideoMode& mode, const QString& aspectChoice) {
    if (mode.formatCode == QStringLiteral("ntsc") || mode.formatCode == QStringLiteral("pal")) {
        return aspectChoice == QStringLiteral("16/9") ? 16.0 / 9.0 : 4.0 / 3.0;
    }
    return mode.height > 0 ? double(mode.width) / mode.height : 16.0 / 9.0;
}

// preview raster: keep the display aspect, cap the height so the pipe stays
// light (bgra at 540 lines is ~2 MB a frame) - both values even
inline void previewSize(int sourceHeight, double aspect, int* width, int* height) {
    int h = qMin(sourceHeight > 0 ? sourceHeight : 540, 540);
    h -= h % 2;
    int w = qRound(h * (aspect > 0 ? aspect : 16.0 / 9.0));
    w -= w % 2;
    *width = qMax(w, 2);
    *height = qMax(h, 2);
}

} // namespace VideoModes
