#include "LevelMeter.h"

#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace {
constexpr float kFloorDb = -60.0f;
constexpr qint64 kHoldMs = 1500;
constexpr float kDecayDbPerTick = 1.5f;   // ~30 dB/s at 50 ms ticks
constexpr int kBarW = 9;
constexpr int kInPairGap = 2;
constexpr int kPairGap = 8;

float clampDb(float db) {
    return std::clamp(db, kFloorDb, 0.0f);
}
}

LevelMeter::LevelMeter(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_clock.start();
    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(50);
    connect(m_decayTimer, &QTimer::timeout, this, &LevelMeter::decay);
    m_decayTimer->start();
}

int LevelMeter::channelCount() const {
    return m_peak.isEmpty() ? m_lastChannels : int(m_peak.size());
}

QSize LevelMeter::sizeHint() const {
    const int channels = std::max(2, channelCount());
    const int pairs = (channels + 1) / 2;
    const int scaleW = fontMetrics().horizontalAdvance(QStringLiteral("-60")) + 6;
    return {scaleW + pairs * (2 * kBarW + kInPairGap) + (pairs + 1) * kPairGap, 240};
}

QSize LevelMeter::minimumSizeHint() const {
    return {sizeHint().width(), 120};
}

void LevelMeter::setMonitorPair(int pair) {
    m_monitorPair = std::max(0, pair);
    update();
}

void LevelMeter::setTitle(const QString& title) {
    m_title = title;
    update();
}

void LevelMeter::setLevels(const QVector<float>& peakDb, const QVector<float>& rmsDb) {
    const int channels = int(peakDb.size());
    if (m_peak.size() != channels) {
        m_peak.fill(kFloorDb, channels);
        m_rms.fill(kFloorDb, channels);
        m_hold.fill(kFloorDb, channels);
        m_holdAt.fill(0, channels);
        if (channels != m_lastChannels && channels > 0) {
            m_lastChannels = channels;
            updateGeometry();   // 2 -> 8 -> 16 channels: the meter widens
        }
    }
    const qint64 now = m_clock.elapsed();
    for (int ch = 0; ch < channels; ++ch) {
        const float peak = clampDb(peakDb.at(ch));
        const float rms = clampDb(rmsDb.value(ch, peak));
        // rise instantly, fall through decay()
        m_peak[ch] = std::max(m_peak[ch], peak);
        m_rms[ch] = std::max(m_rms[ch], rms);
        if (peak >= m_hold[ch] || now - m_holdAt[ch] > kHoldMs) {
            m_hold[ch] = peak;
            m_holdAt[ch] = now;
        }
    }
    m_lastUpdate = now;
    update();
}

void LevelMeter::clear() {
    m_peak.clear();
    m_rms.clear();
    m_hold.clear();
    m_holdAt.clear();
    update();
}

void LevelMeter::decay() {
    if (m_peak.isEmpty()) {
        return;
    }
    // no updates for a while (feed stopped): drop the bars
    if (m_clock.elapsed() - m_lastUpdate > 3000) {
        clear();
        return;
    }
    for (int ch = 0; ch < m_peak.size(); ++ch) {
        m_peak[ch] = std::max(kFloorDb, m_peak[ch] - kDecayDbPerTick);
        m_rms[ch] = std::max(kFloorDb, m_rms[ch] - kDecayDbPerTick);
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());
    const QFontMetrics fm(font());
    const int labelH = fm.height();
    const int scaleW = fm.horizontalAdvance(QStringLiteral("-60")) + 6;
    const int titleH = m_title.isEmpty() ? 0 : labelH + 2;
    const QRect area = rect().adjusted(scaleW, 4 + titleH, -2, -labelH - 4);
    if (!m_title.isEmpty()) {
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QRect(0, 0, width(), labelH + 2), Qt::AlignCenter, m_title);
    }
    painter.fillRect(area, QColor(20, 20, 20));

    const auto yFor = [&](float db) {
        return area.bottom() - int((clampDb(db) - kFloorDb) / -kFloorDb * area.height());
    };
    for (const int db : {0, -6, -12, -18, -24, -36, -48, -60}) {
        const int y = yFor(float(db));
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QRect(0, y - labelH / 2, scaleW - 4, labelH), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
        painter.setPen(QColor(60, 60, 60));
        painter.drawLine(area.left(), y, area.right(), y);
    }

    const int channels = channelCount();
    const int pairs = (channels + 1) / 2;
    for (int pair = 0; pair < pairs; ++pair) {
        const int pairX = area.left() + kPairGap + pair * (2 * kBarW + kInPairGap + kPairGap);
        const QRect pairRect(pairX - 2, area.top(), 2 * kBarW + kInPairGap + 4, area.height());
        if (pair == m_monitorPair) {
            painter.setPen(QPen(QColor(90, 160, 255), 1));
            painter.drawRect(pairRect.adjusted(0, 0, -1, -1));
        }
        for (int side = 0; side < 2; ++side) {
            const int ch = pair * 2 + side;
            if (ch >= channels) {
                break;
            }
            const int x = pairX + side * (kBarW + kInPairGap);
            if (ch < m_peak.size()) {
                const auto zone = [&](float fromDb, float toDb, const QColor& colour, float level) {
                    if (level <= fromDb) {
                        return;
                    }
                    const int top = yFor(std::min(level, toDb));
                    painter.fillRect(QRect(x, top, kBarW, yFor(fromDb) - top), colour);
                };
                zone(kFloorDb, -18.0f, QColor(40, 170, 70), m_rms[ch]);
                zone(-18.0f, -6.0f, QColor(220, 190, 40), m_rms[ch]);
                zone(-6.0f, 0.0f, QColor(220, 50, 40), m_rms[ch]);
                const int peakY = yFor(m_peak[ch]);
                const int rmsY = yFor(m_rms[ch]);
                if (peakY < rmsY) {
                    painter.fillRect(QRect(x, peakY, kBarW, rmsY - peakY), QColor(120, 120, 120, 160));
                }
                const int holdY = yFor(m_hold[ch]);
                painter.fillRect(QRect(x, holdY, kBarW, 2), m_hold[ch] > -1.0f ? QColor(255, 60, 60) : QColor(240, 240, 240));
            }
        }
        painter.setPen(pair == m_monitorPair ? QColor(120, 180, 255) : palette().color(QPalette::WindowText));
        painter.drawText(QRect(pairRect.left() - 4, area.bottom() + 2, pairRect.width() + 8, labelH), Qt::AlignCenter,
                         channels > pair * 2 + 1 ? QStringLiteral("%1/%2").arg(pair * 2 + 1).arg(pair * 2 + 2)
                                                 : QString::number(pair * 2 + 1));
    }
    if (m_peak.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.save();
        painter.translate(area.center());
        painter.rotate(-90);
        painter.drawText(QRect(-area.height() / 2, -labelH, area.height(), 2 * labelH), Qt::AlignCenter, QStringLiteral("no audio"));
        painter.restore();
    }
}
