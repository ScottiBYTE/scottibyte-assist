#include "remote_view.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

RemoteView::RemoteView(
    QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);

    setFocusPolicy(
        Qt::StrongFocus);

    setAttribute(
        Qt::WA_InputMethodEnabled,
        false);

    setMinimumHeight(260);

    setCursor(
        Qt::ArrowCursor);
}

void RemoteView::setFrame(
    const QImage &image)
{
    frame_ = image;

    /*
     * The remote cursor is painted into RemoteView from
     * the customer's transmitted cursor image. Hide the
     * provider's local Qt cursor so it cannot obscure the
     * actual remote arrow, I-beam, hand, resize cursor,
     * or other cursor shape.
     */
    setCursor(
        Qt::BlankCursor);

    update();
}

void RemoteView::clearFrame()
{
    frame_ = {};
    remoteCursorPosition_ = QPoint(-1, -1);
    remoteCursorPositionConfirmed_ = false;
    remoteCursorImage_ = {};
    remoteCursorHotspot_ = QPoint(0, 0);

    setCursor(
        Qt::ArrowCursor);

    update();
}

void RemoteView::setRemoteCursorPosition(
    int x,
    int y)
{
    remoteCursorPosition_ =
        QPoint(x, y);

    update();
}

void RemoteView::setRemoteCursorPositionFromPeer(
    int x,
    int y)
{
    remoteCursorPositionConfirmed_ = true;

    setRemoteCursorPosition(
        x,
        y);
}

void RemoteView::setRemoteCursorImage(
    const QImage &image,
    int hotspotX,
    int hotspotY)
{
    remoteCursorImage_ = image;
    remoteCursorHotspot_ =
        QPoint(
            hotspotX,
            hotspotY);

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

    const QRect target =
        imageRect();

    painter.drawImage(
        target,
        frame_);

    if (remoteCursorPosition_.x() >= 0 &&
        remoteCursorPosition_.y() >= 0 &&
        frame_.width() > 0 &&
        frame_.height() > 0) {
        const int cursorX =
            target.left() +
            remoteCursorPosition_.x() *
                target.width() /
                frame_.width();

        const int cursorY =
            target.top() +
            remoteCursorPosition_.y() *
                target.height() /
                frame_.height();

        if (!remoteCursorImage_.isNull()) {
            const double scaleX =
                static_cast<double>(
                    target.width()) /
                static_cast<double>(
                    frame_.width());

            const double scaleY =
                static_cast<double>(
                    target.height()) /
                static_cast<double>(
                    frame_.height());

            const int cursorWidth =
                qMax(
                    1,
                    qRound(
                        remoteCursorImage_.width() *
                        scaleX));

            const int cursorHeight =
                qMax(
                    1,
                    qRound(
                        remoteCursorImage_.height() *
                        scaleY));

            const int hotspotX =
                qRound(
                    remoteCursorHotspot_.x() *
                    scaleX);

            const int hotspotY =
                qRound(
                    remoteCursorHotspot_.y() *
                    scaleY);

            const QRect cursorRect(
                cursorX - hotspotX,
                cursorY - hotspotY,
                cursorWidth,
                cursorHeight);

            painter.drawImage(
                cursorRect,
                remoteCursorImage_);

            return;
        }

        /*
         * Wayland embeds its native cursor in the captured
         * desktop frame and does not send a separate cursor
         * position through DesktopBackend. Do not draw the
         * generic predicted fallback arrow unless the remote
         * machine has actually supplied a cursor position.
         */
        if (!remoteCursorPositionConfirmed_) {
            return;
        }

        const QPoint tip(
            cursorX,
            cursorY);

        QPolygon arrow;
        arrow
            << tip
            << QPoint(cursorX + 5, cursorY + 18)
            << QPoint(cursorX + 9, cursorY + 12)
            << QPoint(cursorX + 15, cursorY + 18)
            << QPoint(cursorX + 18, cursorY + 15)
            << QPoint(cursorX + 12, cursorY + 9)
            << QPoint(cursorX + 18, cursorY + 5);

        painter.setPen(
            QPen(Qt::black, 3));
        painter.setBrush(
            Qt::white);
        painter.drawPolygon(
            arrow);
    }
}

void RemoteView::mouseMoveEvent(
    QMouseEvent *event)
{
    const QPoint position =
        imagePosition(
            event->position()
                .toPoint());

    if (position.x() >= 0) {
        /*
         * Render provider mouse movement immediately.
         *
         * Waiting for the pointer move to travel to the
         * remote machine and for the resulting cursor
         * position to travel back creates visible
         * round-trip cursor latency.
         *
         * The remote cursor-position messages remain
         * authoritative and will correct this predicted
         * position when they arrive.
         */
        setRemoteCursorPosition(
            position.x(),
            position.y());

        emit pointerMoveRequested(
            position.x(),
            position.y());
    }
}

void RemoteView::mousePressEvent(
    QMouseEvent *event)
{
    if (event->button() !=
            Qt::LeftButton &&
        event->button() !=
            Qt::RightButton) {
        return;
    }

    const QPoint position =
        imagePosition(
            event->position()
                .toPoint());

    if (position.x() >= 0) {
        setFocus(
            Qt::MouseFocusReason);
    }

    if (position.x() < 0) {
        return;
    }

    if (event->button() ==
        Qt::LeftButton) {
        emit leftButtonPressRequested(
            position.x(),
            position.y());
    } else {
        emit rightClickRequested(
            position.x(),
            position.y());
    }
}

void RemoteView::mouseReleaseEvent(
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
        emit leftButtonReleaseRequested(
            position.x(),
            position.y());
    }
}

void RemoteView::wheelEvent(
    QWheelEvent *event)
{
    const int delta =
        event->angleDelta().y();

    if (delta == 0) {
        QWidget::wheelEvent(event);
        return;
    }

    emit wheelRequested(
        delta);

    event->accept();
}

void RemoteView::keyPressEvent(
    QKeyEvent *event)
{
    emit keyPressRequested(
        event->key());

    event->accept();
}

void RemoteView::keyReleaseEvent(
    QKeyEvent *event)
{
    emit keyReleaseRequested(
        event->key());

    event->accept();
}

