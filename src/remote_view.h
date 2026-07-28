#pragma once

#include <QImage>
#include <QWidget>

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

    void fullScreenRequested();

protected:
    void paintEvent(
        QPaintEvent *event) override;

    void mouseMoveEvent(
        QMouseEvent *event) override;

    void mousePressEvent(
        QMouseEvent *event) override;

    void mouseReleaseEvent(
        QMouseEvent *event) override;

    void mouseDoubleClickEvent(
        QMouseEvent *event) override;

private:
    QRect imageRect() const;

    QPoint imagePosition(
        const QPoint &widgetPosition) const;

    QImage frame_;
};
