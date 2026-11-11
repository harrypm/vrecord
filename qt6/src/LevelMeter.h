#pragma once

// Per-channel audio level meter (dBFS), laid out as stereo pairs (1/2, 3/4,
// ... up to 15/16): RMS bar, peak cap and a short peak hold on a -60..0 dB
// scale. One pair is highlighted - the monitored pair (1/2 by default, the
// usual stereo of most sources). Tall and narrow, so it sits beside a video
// pane. Fed about 20 times a second; bars decay between updates.

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

class QTimer;

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);

    void setLevels(const QVector<float>& peakDb, const QVector<float>& rmsDb);
    void clear();
    // 0 = channels 1/2, 1 = 3/4, ...
    void setMonitorPair(int pair);
    void setTitle(const QString& title);
    // show only the monitored pair (2 bars wide), labelled with its channels
    void setPairOnly(bool pairOnly);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void decay();
    int channelCount() const;

    QVector<float> m_peak;      // shown peak (dB)
    QVector<float> m_rms;       // shown RMS (dB)
    QVector<float> m_hold;      // peak hold (dB)
    QVector<qint64> m_holdAt;   // ms of the hold
    QElapsedTimer m_clock;
    qint64 m_lastUpdate = 0;
    QTimer* m_decayTimer = nullptr;
    int m_monitorPair = 0;
    int m_lastChannels = 2;     // keeps the width while no audio arrives
    QString m_title;
    bool m_pairOnly = false;
};
