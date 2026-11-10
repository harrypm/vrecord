#include "VideoView.h"

#include <QPainter>

VideoView::VideoView(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_clock.start();
}

void VideoView::setFrame(const QImage& frame) {
    if (frame.isNull()) {
        return;
    }
    const qint64 now = m_clock.nsecsElapsed();
    if (m_lastFrameNs > 0) {
        const double instant = 1e9 / double(qMax<qint64>(now - m_lastFrameNs, 1));
        // smoothed for the on-screen readout only
        m_fps = m_fps <= 0 ? instant : m_fps * 0.9 + instant * 0.1;
    }
    m_lastFrameNs = now;
    m_frame = frame;
    m_message.clear();
    update();
}

void VideoView::clearFrame(const QString& message) {
    m_frame = QImage();
    m_message = message;
    m_lastFrameNs = 0;
    m_fps = 0.0;
    update();
}

void VideoView::setMessage(const QString& message) {
    m_message = message;
    update();
}

void VideoView::setCaption(const QString& caption) {
    m_caption = caption;
    update();
}

void VideoView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_frame.isNull()) {
        const QSize fitted = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
        // nearest-neighbour scaling keeps the paint cheap at 50/60 fps; the
        // feed is already scaled to roughly the pane size by ffmpeg
        painter.drawImage(target, m_frame);
    }
    painter.setPen(Qt::white);
    if (!m_message.isEmpty()) {
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
    QString overlay = m_caption;
    if (!m_frame.isNull() && m_fps > 0) {
        overlay += (overlay.isEmpty() ? QString() : QStringLiteral("  ")) + QStringLiteral("%1 fps").arg(m_fps, 0, 'f', 2);
    }
    if (!overlay.isEmpty()) {
        const QRect band(0, height() - 22, width(), 22);
        painter.fillRect(band, QColor(0, 0, 0, 150));
        painter.drawText(band.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, overlay);
    }
}
