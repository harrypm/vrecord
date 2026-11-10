#pragma once

// FrameFeed runs one ffmpeg process whose stdout carries raw BGRA frames of a
// fixed size (-f rawvideo -pix_fmt bgra pipe:1) and hands the newest frame to
// the GUI as fast as ffmpeg produces it: no fps decimation, no image files on
// disk, no polling timer. The same ffmpeg process can also drive the DeckLink
// (input capture or output playout), so the pane shows exactly the frames the
// card is taking in or sending out.
//
// A feed can carry several launch attempts (e.g. input autodetect, then the
// explicit format code): when a process ends before delivering its first
// frame the next attempt is started.

#include <QImage>
#include <QList>
#include <QMutex>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <deque>

class FrameFeed : public QThread {
    Q_OBJECT
public:
    struct Attempt {
        QString label;
        QStringList arguments;
    };

    FrameFeed(const QString& program, const QList<Attempt>& attempts, int width, int height, QObject* parent = nullptr);
    ~FrameFeed() override;

    // hold the frames back by this many frames before showing them (the
    // DeckLink output preroll, so the pane matches what is on the wire)
    void setDisplayDelayFrames(int frames) { m_delayFrames = qMax(0, frames); }
    // append every launch (command line) and its outcome to this file
    void setLogPath(const QString& path) { m_logPath = path; }

    void stop();
    // the playout was frozen: show the newest frame now (the card plays out
    // its preroll and holds the last scheduled frame, which is this one)
    void flushDelay() { m_flushRequested = true; }
    qint64 framesReceived() const { return m_framesReceived.load(); }
    QImage takeFrame();
    qint64 framesDelivered() const { return m_framesDelivered.load(); }
    qint64 processId() const { return m_pid.load(); }
    bool isStreaming() const { return m_streaming.load(); }
    QString activeLabel() const;
    QString stderrTail() const;
    int frameWidth() const { return m_width; }
    int frameHeight() const { return m_height; }

signals:
    void frameReady();                       // coalesced: call takeFrame()
    void streaming(const QString& label);    // first frame of an attempt arrived
    void attemptFailed(const QString& label, const QString& message);
    void ended(bool hadFrames, const QString& message);  // process gone (not on stop())

protected:
    void run() override;

private:
    void deliver(const QImage& frame);
    void appendStderr(const QByteArray& data);
    void log(const QString& line) const;

    const QString m_program;
    const QList<Attempt> m_attempts;
    const int m_width;
    const int m_height;
    int m_delayFrames = 0;
    QString m_logPath;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_pending{false};
    std::atomic<bool> m_streaming{false};
    std::atomic<qint64> m_framesDelivered{0};
    std::atomic<qint64> m_framesReceived{0};
    std::atomic<bool> m_flushRequested{false};
    std::atomic<qint64> m_pid{0};
    mutable QMutex m_mutex;
    QImage m_latest;
    QString m_activeLabel;
    QByteArray m_stderrTail;
    std::deque<QImage> m_delayQueue;
};
