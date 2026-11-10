#pragma once

// Paints the newest frame of a FrameFeed, letterboxed to the frame's aspect.
// Repaints are driven by frame arrival (not a timer), so the pane updates at
// the source frame rate.

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void setFrame(const QImage& frame);
    void clearFrame(const QString& message = {});
    void setMessage(const QString& message);
    void setCaption(const QString& caption);
    double measuredFps() const { return m_fps; }
    QImage currentFrame() const { return m_frame; }

    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_frame;
    QString m_message;
    QString m_caption;
    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    double m_fps = 0.0;
};
