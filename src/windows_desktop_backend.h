#pragma once

#include "desktop_backend.h"

class WindowsDesktopBackend final
    : public DesktopBackend
{
    Q_OBJECT

public:
    explicit WindowsDesktopBackend(
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

    void pressLeftAt(
        int x,
        int y) override;

    void releaseLeftAt(
        int x,
        int y) override;

    void clickRightAt(
        int x,
        int y) override;

    void pressKey(
        int qtKey) override;

    void releaseKey(
        int qtKey) override;
};
