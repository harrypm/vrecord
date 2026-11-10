#include "VideoView.h"

#include <QPainter>

#include <cmath>

VideoView::VideoView(QWidget* parent)
    : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    m_clock.start();
}

int VideoView::heightForWidth(int width) const {
    return qMax(90, qRound(width / m_aspect));
}

QSize VideoView::sizeHint() const {
    return {640, heightForWidth(640)};
}

QSize VideoView::minimumSizeHint() const {
    return {240, heightForWidth(240)};
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
    // a feed with a new shape (another input mode, another file) re-shapes
    // the widget so the picture keeps filling it
    const double aspect = frame.height() > 0 ? double(frame.width()) / frame.height() : m_aspect;
    if (std::abs(aspect - m_aspect) > 0.005) {
        m_aspect = aspect;
        updateGeometry();
    }
    m_frame = frame;
    m_message.clear();
    update();
}

void VideoView::clearFrame(const QString& message) {
    // keep the last shape so the pane does not jump while a feed restarts
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

// the picture area: the whole widget when the layout honoured the aspect,
// otherwise the largest centred rectangle of that aspect
QRect VideoView::pictureRect() const {
    const QSize fitted = QSize(qRound(1000 * m_aspect), 1000).scaled(size(), Qt::KeepAspectRatio);
    return {QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted};
}

void VideoView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QRect picture = pictureRect();
    // outside the picture: plain window colour, not black bars
    painter.fillRect(rect(), palette().window());
    painter.fillRect(picture, Qt::black);
    if (!m_frame.isNull()) {
        // nearest-neighbour scaling keeps the paint cheap at 50/60 fps; the
        // feed is already scaled to roughly the pane size by ffmpeg
        painter.drawImage(picture, m_frame);
    }
    painter.setPen(Qt::white);
    if (!m_message.isEmpty()) {
        painter.drawText(picture.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
    QString overlay = m_caption;
    if (!m_frame.isNull() && m_fps > 0) {
        overlay += (overlay.isEmpty() ? QString() : QStringLiteral("  ")) + QStringLiteral("%1 fps").arg(m_fps, 0, 'f', 2);
    }
    if (!overlay.isEmpty()) {
        const QRect band(picture.left(), picture.bottom() - 21, picture.width(), 22);
        painter.fillRect(band, QColor(0, 0, 0, 150));
        painter.drawText(band.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, overlay);
    }
}
