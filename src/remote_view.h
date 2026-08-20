#pragma once

#include <QImage>
#include <QWidget>

class QKeyEvent;
class QWheelEvent;

class RemoteView final : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteView(
        QWidget *parent = nullptr);

public slots:
    void setFrame(
        const QImage &image);

    void clearFrame();

    void setRemoteCursorPosition(
        int x,
        int y);

    void setRemoteCursorPositionFromPeer(
        int x,
        int y);

    void setRemoteCursorImage(
        const QImage &image,
        int hotspotX,
        int hotspotY);

signals:
    void pointerMoveRequested(
        int x,
        int y);

    void leftClickRequested(
        int x,
        int y);

    void leftButtonPressRequested(
        int x,
        int y);

    void leftButtonReleaseRequested(
        int x,
        int y);

    void rightClickRequested(
        int x,
        int y);

    void wheelRequested(
        int delta);

    void keyPressRequested(
        int qtKey);

    void keyReleaseRequested(
        int qtKey);

protected:
    void paintEvent(
        QPaintEvent *event) override;

    void mouseMoveEvent(
        QMouseEvent *event) override;

    void mousePressEvent(
        QMouseEvent *event) override;

    void mouseReleaseEvent(
        QMouseEvent *event) override;

    void wheelEvent(
        QWheelEvent *event) override;

    void keyPressEvent(
        QKeyEvent *event) override;

    void keyReleaseEvent(
        QKeyEvent *event) override;

private:
    QRect imageRect() const;

    QPoint imagePosition(
        const QPoint &widgetPosition) const;

    QImage frame_;
    QPoint remoteCursorPosition_{-1, -1};
    bool remoteCursorPositionConfirmed_ = false;
    QImage remoteCursorImage_;
    QPoint remoteCursorHotspot_{0, 0};
};
