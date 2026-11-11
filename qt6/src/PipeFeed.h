#pragma once

// PipeFeed: a Windows named-pipe server that receives raw BGRA frames of a
// fixed size from an ffmpeg running elsewhere (the vrecord shell backend's
// monitor player when the ffplay window is off) and hands the newest frame to
// the GUI, like FrameFeed does for the processes it starts itself.
// Closing the pipe (stop()) makes the writer fail and exit - for a record
// session that ends the capture the same way closing the ffplay window does.

#include <QImage>
#include <QMutex>
#include <QString>
#include <QThread>

#include <atomic>

class PipeFeed : public QThread {
    Q_OBJECT
public:
    PipeFeed(const QString& pipeName, int width, int height, QObject* parent = nullptr);
    ~PipeFeed() override;

    // what the writer opens: //./pipe/<name>
    QString clientPath() const { return QStringLiteral("//./pipe/%1").arg(m_name); }
    void stop();
    QImage takeFrame();
    qint64 framesReceived() const { return m_frames.load(); }

signals:
    void frameReady();
    void connected();
    void ended();

protected:
    void run() override;

private:
    const QString m_name;
    const int m_width;
    const int m_height;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_pending{false};
    std::atomic<qint64> m_frames{0};
    std::atomic<void*> m_pipe{nullptr};
    std::atomic<void*> m_thread{nullptr};
    QMutex m_mutex;
    QImage m_latest;
};
