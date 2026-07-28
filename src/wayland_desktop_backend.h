#pragma once

#include "desktop_backend.h"

class LibeiInput;
class PipeWirePreview;
class PortalSession;

class WaylandDesktopBackend final
    : public DesktopBackend
{
    Q_OBJECT

public:
    explicit WaylandDesktopBackend(
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

private slots:
    void handleFrame(
        const QImage &frame);

private:
    PortalSession *portalSession_ = nullptr;
    PipeWirePreview *pipeWirePreview_ = nullptr;
    LibeiInput *libeiInput_ = nullptr;

    int frameWidth_ = 0;
    int frameHeight_ = 0;

    int previousPointerX_ = 0;
    int previousPointerY_ = 0;
    bool previousPointerValid_ = false;
};
