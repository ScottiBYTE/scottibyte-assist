#pragma once

#include "desktop_backend.h"

#include <QTimer>

class QScreen;

class WindowsDesktopBackend final
    : public DesktopBackend
{
    Q_OBJECT

public:
    explicit WindowsDesktopBackend(
        QObject *parent = nullptr);

    bool isSupported() const override;

    QList<DisplaySource>
        availableRemoteControlDisplays() const override;

    bool setRemoteControlDisplay(
        const QString &displayId) override;

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

    void scrollWheel(
        int delta);

    void pressKey(
        int qtKey) override;

    void releaseKey(
        int qtKey) override;

private slots:
    void captureFrame();

private:
    QScreen *selectedScreen() const;

    bool desktopPointForFramePoint(
        int x,
        int y,
        int &desktopX,
        int &desktopY) const;

    QTimer captureTimer_;
    bool running_ = false;

    int selectedScreenIndex_ = -1;
    int frameWidth_ = 0;
    int frameHeight_ = 0;

    void *lastCursorHandle_ = nullptr;
};
