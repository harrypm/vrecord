#pragma once

// Paints the newest frame of a FrameFeed. The widget takes the frame's own
// shape (height follows width at the frame aspect, re-evaluated when the feed
// changes raster), so the picture fills it without black borders; any space
// the layout cannot give up is left in the window colour, not painted black.
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

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect pictureRect() const;

    QImage m_frame;
    QString m_message;
    QString m_caption;
    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    double m_fps = 0.0;
    double m_aspect = 16.0 / 9.0;   // of the current (or last) feed
};
