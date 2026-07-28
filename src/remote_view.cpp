#include "remote_view.h"

#include <QMouseEvent>
#include <QPainter>

RemoteView::RemoteView(
    QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);

    setMinimumHeight(260);

    setCursor(
        Qt::ArrowCursor);
}

void RemoteView::setFrame(
    const QImage &image)
{
    frame_ = image;

    if (frame_.isNull()) {
        unsetCursor();
    } else {
        setCursor(
            Qt::BlankCursor);
    }

    update();
}

void RemoteView::clearFrame()
{
    frame_ = {};
    update();
}

QRect RemoteView::imageRect() const
{
    if (frame_.isNull()) {
        return {};
    }

    QSize scaledSize =
        frame_.size();

    scaledSize.scale(
        size(),
        Qt::KeepAspectRatio);

    const int x =
        (width() -
         scaledSize.width()) / 2;

    const int y =
        (height() -
         scaledSize.height()) / 2;

    return QRect(
        QPoint(x, y),
        scaledSize);
}

QPoint RemoteView::imagePosition(
    const QPoint &widgetPosition) const
{
    const QRect target =
        imageRect();

    if (frame_.isNull() ||
        target.isEmpty() ||
        !target.contains(
            widgetPosition)) {
        return QPoint(-1, -1);
    }

    const double relativeX =
        static_cast<double>(
            widgetPosition.x() -
            target.left()) /
        static_cast<double>(
            target.width());

    const double relativeY =
        static_cast<double>(
            widgetPosition.y() -
            target.top()) /
        static_cast<double>(
            target.height());

    const int imageX =
        qBound(
            0,
            static_cast<int>(
                relativeX *
                frame_.width()),
            frame_.width() - 1);

    const int imageY =
        qBound(
            0,
            static_cast<int>(
                relativeY *
                frame_.height()),
            frame_.height() - 1);

    return QPoint(
        imageX,
        imageY);
}

void RemoteView::paintEvent(
    QPaintEvent *)
{
    QPainter painter(this);

    painter.fillRect(
        rect(),
        QColor(
            4,
            19,
            39));

    if (frame_.isNull()) {
        painter.setPen(
            QColor(
                125,
                234,
                255));

        painter.drawText(
            rect(),
            Qt::AlignCenter,
            QStringLiteral(
                "The remote desktop will appear here."));

        return;
    }

    painter.setRenderHint(
        QPainter::SmoothPixmapTransform,
        true);

    painter.drawImage(
        imageRect(),
        frame_);
}

void RemoteView::mouseMoveEvent(
    QMouseEvent *event)
{
    const QPoint position =
        imagePosition(
            event->position()
                .toPoint());

    if (position.x() >= 0) {
        emit pointerMoveRequested(
            position.x(),
            position.y());
    }
}

void RemoteView::mousePressEvent(
    QMouseEvent *event)
{
    if (event->button() !=
        Qt::LeftButton) {
        return;
    }

    const QPoint position =
        imagePosition(
            event->position()
                .toPoint());

    if (position.x() >= 0) {
        emit leftClickRequested(
            position.x(),
            position.y());
    }
}

void RemoteView::mouseDoubleClickEvent(
    QMouseEvent *event)
{
    if (event->button() ==
        Qt::LeftButton) {
        emit fullScreenRequested();
    }
}
