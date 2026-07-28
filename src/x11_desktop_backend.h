#pragma once

#include "desktop_backend.h"

class QTimer;

class X11DesktopBackend final
    : public DesktopBackend
{
    Q_OBJECT

public:
    explicit X11DesktopBackend(
        QObject *parent = nullptr);

    bool isSupported() const override;

public slots:
    void start() override;
    void stop() override;

    void movePointerTo(
        int x,
        int y) override;

    void clickLeftAt(
        int x,
        int y) override;

    void clickRightAt(
        int x,
        int y) override;

private slots:
    void captureFrame();

private:
    QTimer *captureTimer_ = nullptr;
};
