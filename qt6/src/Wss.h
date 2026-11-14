#pragma once

// PAL widescreen signalling (WSS, ETSI EN 300 294 / ITU-R BT.1119): 14 bits
// on line 23 that tell a TV the picture's aspect ratio. Line 23 is the top
// row of a 576-line SD capture, so the aspect of a PAL source (an anamorphic
// 16:9 DVD player, a widescreen broadcast recording, a deck passing WSS on)
// can be read from the captured picture itself.
//
// Line layout (5 MHz element rate, about 2.7 samples per element at the
// BT.601 13.5 MHz sampling of a DeckLink capture):
//   run-in    29 elements  0x1F1C71C7
//   start     24 elements  0x1E3C1F
//   data      14 bits, each biphase over 6 elements (0 = 000111, 1 = 111000)
// The first 4 data bits (b0..b3, b3 = odd parity) are the aspect group:
//   0001 4:3 full format        1000 14:9 letterbox centre
//   0100 14:9 letterbox top     1101 16:9 letterbox centre
//   0010 16:9 letterbox top     1011 >16:9 letterbox centre
//   0111 14:9 full format       1110 16:9 full format (anamorphic)
// Only 1110 is an anamorphic 16:9 picture; every other group is a 4:3 frame.

#include <QImage>
#include <QString>
#include <QVector>

#include <cmath>

namespace Wss {

enum class Aspect { None, FourThree, Wide };

struct Result {
    Aspect aspect = Aspect::None;
    QString group;         // "b0b1b2b3", e.g. "1110"
    QString description;   // "16:9 full format (anamorphic)"
};

constexpr double kSamplesPerElement = 13.5 / 5.0;
constexpr int kDataBits = 14;

// run-in + start code, one value per element (1 = high), first sent first
inline const QVector<int>& preamble() {
    static const QVector<int> sequence = [] {
        QVector<int> elements;
        const quint32 runIn = 0x1F1C71C7u;
        for (int i = 28; i >= 0; --i) {
            elements << int((runIn >> i) & 1u);
        }
        const quint32 start = 0x1E3C1Fu;
        for (int i = 23; i >= 0; --i) {
            elements << int((start >> i) & 1u);
        }
        return elements;
    }();
    return sequence;
}

inline QString describeGroup(const QString& group) {
    if (group == QStringLiteral("0001")) return QStringLiteral("4:3 full format");
    if (group == QStringLiteral("1000")) return QStringLiteral("14:9 letterbox centre");
    if (group == QStringLiteral("0100")) return QStringLiteral("14:9 letterbox top");
    if (group == QStringLiteral("1101")) return QStringLiteral("16:9 letterbox centre");
    if (group == QStringLiteral("0010")) return QStringLiteral("16:9 letterbox top");
    if (group == QStringLiteral("1011")) return QStringLiteral(">16:9 letterbox centre");
    if (group == QStringLiteral("0111")) return QStringLiteral("14:9 full format");
    if (group == QStringLiteral("1110")) return QStringLiteral("16:9 full format (anamorphic)");
    return {};
}

inline float sampleAt(const QVector<float>& line, double position) {
    const int i = int(std::floor(position));
    if (i < 0 || i + 1 >= line.size()) {
        return line.isEmpty() ? 0.f : line.at(qBound(0, i, int(line.size()) - 1));
    }
    const float t = float(position - i);
    return line.at(i) * (1.f - t) + line.at(i + 1) * t;
}

// line: luma per sample of one 720-sample line (any scale, e.g. 0-255)
inline Result decode(const QVector<float>& line) {
    Result result;
    const QVector<int>& pre = preamble();
    const int elements = int(pre.size()) + kDataBits * 6;
    const double length = elements * kSamplesPerElement;
    if (line.size() < int(length) + 4) {
        return result;
    }
    // levels from the part of the line WSS occupies (the rest of line 23
    // can carry picture, brighter than the WSS white level)
    const int span = qMin(int(line.size()), int(length) + 30);
    float low = line.at(0);
    float high = line.at(0);
    for (int i = 1; i < span; ++i) {
        low = qMin(low, line.at(i));
        high = qMax(high, line.at(i));
    }
    const float amplitude = high - low;
    if (amplitude < 40.f) {   // flat line: no WSS
        return result;
    }
    const float threshold = (high + low) / 2.f;
    const auto element = [&](double offset, int k) {
        return sampleAt(line, offset + (k + 0.5) * kSamplesPerElement) - threshold;
    };

    // find where the run-in + start code sit (the start varies a little
    // between sources and cards): best normalised correlation
    double bestOffset = -1;
    double bestScore = 0;
    const double maxOffset = qMin(120.0, line.size() - length - 2);
    for (double offset = 0; offset <= maxOffset; offset += 0.25) {
        double score = 0;
        double norm = 0;
        for (int k = 0; k < pre.size(); ++k) {
            const float v = element(offset, k);
            score += (pre.at(k) ? 1.0 : -1.0) * v;
            norm += std::fabs(v);
        }
        if (norm > 0 && score / norm > bestScore) {
            bestScore = score / norm;
            bestOffset = offset;
        }
    }
    if (bestOffset < 0 || bestScore < 0.85) {
        return result;
    }

    QString bits;
    for (int bit = 0; bit < 4; ++bit) {
        const int first = int(pre.size()) + bit * 6;
        const float a = (element(bestOffset, first) + element(bestOffset, first + 1) + element(bestOffset, first + 2)) / 3.f;
        const float b = (element(bestOffset, first + 3) + element(bestOffset, first + 4) + element(bestOffset, first + 5)) / 3.f;
        if (std::fabs(a - b) < 0.3f * amplitude) {
            return result;   // no clear biphase transition: not a valid bit
        }
        bits += a > b ? QLatin1Char('1') : QLatin1Char('0');
    }
    if (bits.count(QLatin1Char('1')) % 2 != 1) {   // b3 is odd parity
        return result;
    }
    const QString description = describeGroup(bits);
    if (description.isEmpty()) {
        return result;
    }
    result.group = bits;
    result.description = description;
    result.aspect = bits == QStringLiteral("1110") ? Aspect::Wide : Aspect::FourThree;
    return result;
}

// luma of one image row, the first `samples` pixels
inline QVector<float> rowLuma(const QImage& image, int row, int samples = 720) {
    QVector<float> line;
    if (row < 0 || row >= image.height()) {
        return line;
    }
    const int count = qMin(samples, image.width());
    line.reserve(count);
    const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(row));
    for (int x = 0; x < count; ++x) {
        const QRgb p = pixels[x];
        line << float(0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p));
    }
    return line;
}

// a synthetic WSS line (for the self-test): group "b0b1b2b3", the remaining
// 10 bits zero, starting `offset` samples into a 720-sample line
inline QVector<float> synthesizeLine(const QString& group, double offset, float black = 16.f, float white = 180.f) {
    QVector<int> elements = preamble();
    for (int bit = 0; bit < kDataBits; ++bit) {
        const bool one = bit < group.size() && group.at(bit) == QLatin1Char('1');
        for (int e = 0; e < 6; ++e) {
            elements << int(one ? e < 3 : e >= 3);
        }
    }
    QVector<float> line(720, black);
    for (int x = 0; x < line.size(); ++x) {
        const int k = int(std::floor((x - offset) / kSamplesPerElement));
        if (k >= 0 && k < elements.size()) {
            line[x] = elements.at(k) ? white : black;
        }
    }
    return line;
}

} // namespace Wss
