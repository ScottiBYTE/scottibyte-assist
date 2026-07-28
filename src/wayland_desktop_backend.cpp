#include "wayland_desktop_backend.h"

#include "libei_input.h"
#include "pipewire_preview.h"
#include "portal_session.h"

WaylandDesktopBackend::WaylandDesktopBackend(
    QObject *parent)
    : DesktopBackend(parent),
      portalSession_(
          new PortalSession(this)),
      pipeWirePreview_(
          new PipeWirePreview(this)),
      libeiInput_(
          new LibeiInput(this))
{
    connect(
        portalSession_,
        &PortalSession::pipeWireStreamReady,
        pipeWirePreview_,
        &PipeWirePreview::start);

    connect(
        portalSession_,
        &PortalSession::eisConnectionReady,
        libeiInput_,
        &LibeiInput::start);

    connect(
        pipeWirePreview_,
        &PipeWirePreview::frameReady,
        this,
        &WaylandDesktopBackend::handleFrame);

    connect(
        portalSession_,
        &PortalSession::statusChanged,
        this,
        &WaylandDesktopBackend::statusChanged);

    connect(
        pipeWirePreview_,
        &PipeWirePreview::statusChanged,
        this,
        &WaylandDesktopBackend::statusChanged);

    connect(
        libeiInput_,
        &LibeiInput::statusChanged,
        this,
        &WaylandDesktopBackend::statusChanged);

    connect(
        pipeWirePreview_,
        &PipeWirePreview::errorOccurred,
        this,
        &WaylandDesktopBackend::errorOccurred);

    connect(
        libeiInput_,
        &LibeiInput::errorOccurred,
        this,
        &WaylandDesktopBackend::errorOccurred);
}

bool WaylandDesktopBackend::isSupported() const
{
    return portalSession_->isSupported();
}

void WaylandDesktopBackend::start()
{
    if (!isSupported()) {
        emit errorOccurred(
            QStringLiteral(
                "The Wayland remote-desktop portals "
                "are unavailable."));
        return;
    }

    frameWidth_ = 0;
    frameHeight_ = 0;
    previousPointerValid_ = false;

    portalSession_->start();
}

void WaylandDesktopBackend::stop()
{
    pipeWirePreview_->stop();
    libeiInput_->stop();
    portalSession_->stop();

    frameWidth_ = 0;
    frameHeight_ = 0;
    previousPointerValid_ = false;
}

void WaylandDesktopBackend::handleFrame(
    const QImage &frame)
{
    if (!frame.isNull()) {
        frameWidth_ = frame.width();
        frameHeight_ = frame.height();

        emit frameReady(frame);
    }

    pipeWirePreview_->acknowledgeFrame();
}

void WaylandDesktopBackend::movePointerTo(
    int x,
    int y)
{
    if (frameWidth_ <= 0 ||
        frameHeight_ <= 0) {
        return;
    }

    if (libeiInput_->absolutePointerReady()) {
        libeiInput_->movePointerAbsolute(
            x,
            y,
            frameWidth_,
            frameHeight_);

        previousPointerX_ = x;
        previousPointerY_ = y;
        previousPointerValid_ = true;
        return;
    }

    if (!libeiInput_->pointerReady()) {
        return;
    }

    if (!previousPointerValid_) {
        previousPointerX_ = x;
        previousPointerY_ = y;
        previousPointerValid_ = true;
        return;
    }

    const int deltaX =
        x - previousPointerX_;

    const int deltaY =
        y - previousPointerY_;

    previousPointerX_ = x;
    previousPointerY_ = y;

    libeiInput_->movePointerRelative(
        deltaX,
        deltaY);
}

void WaylandDesktopBackend::clickLeftAt(
    int x,
    int y)
{
    if (frameWidth_ <= 0 ||
        frameHeight_ <= 0) {
        return;
    }

    movePointerTo(
        x,
        y);

    if (libeiInput_->buttonReady()) {
        libeiInput_->clickLeftButton();
    }
}

void WaylandDesktopBackend::clickRightAt(
    int x,
    int y)
{
    if (frameWidth_ <= 0 ||
        frameHeight_ <= 0) {
        return;
    }

    movePointerTo(
        x,
        y);

    if (libeiInput_->buttonReady()) {
        libeiInput_->clickRightButton();
    }
}
