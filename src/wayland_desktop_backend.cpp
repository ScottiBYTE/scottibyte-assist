#include "wayland_desktop_backend.h"

#include "libei_input.h"
#include "pipewire_preview.h"
#include "portal_session.h"

#include <Qt>

#include <linux/input-event-codes.h>

namespace
{

uint32_t linuxKeyCodeForQtKey(
    int qtKey)
{
    if (qtKey >= Qt::Key_A &&
        qtKey <= Qt::Key_Z) {
        static constexpr uint32_t codes[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
            KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
            KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
            KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
            KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y,
            KEY_Z
        };

        return codes[
            qtKey - Qt::Key_A];
    }

    if (qtKey >= Qt::Key_0 &&
        qtKey <= Qt::Key_9) {
        static constexpr uint32_t codes[] = {
            KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
            KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
        };

        return codes[
            qtKey - Qt::Key_0];
    }

    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return KEY_ENTER;
    case Qt::Key_Backspace:
        return KEY_BACKSPACE;
    case Qt::Key_Tab:
        return KEY_TAB;
    case Qt::Key_Space:
        return KEY_SPACE;
    case Qt::Key_Escape:
        return KEY_ESC;
    case Qt::Key_Left:
        return KEY_LEFT;
    case Qt::Key_Right:
        return KEY_RIGHT;
    case Qt::Key_Up:
        return KEY_UP;
    case Qt::Key_Down:
        return KEY_DOWN;
    case Qt::Key_Home:
        return KEY_HOME;
    case Qt::Key_End:
        return KEY_END;
    case Qt::Key_PageUp:
        return KEY_PAGEUP;
    case Qt::Key_PageDown:
        return KEY_PAGEDOWN;
    case Qt::Key_Insert:
        return KEY_INSERT;
    case Qt::Key_Delete:
        return KEY_DELETE;
    case Qt::Key_Shift:
        return KEY_LEFTSHIFT;
    case Qt::Key_Control:
        return KEY_LEFTCTRL;
    case Qt::Key_Alt:
        return KEY_LEFTALT;
    case Qt::Key_Meta:
        return KEY_LEFTMETA;
    case Qt::Key_F1:
        return KEY_F1;
    case Qt::Key_F2:
        return KEY_F2;
    case Qt::Key_F3:
        return KEY_F3;
    case Qt::Key_F4:
        return KEY_F4;
    case Qt::Key_F5:
        return KEY_F5;
    case Qt::Key_F6:
        return KEY_F6;
    case Qt::Key_F7:
        return KEY_F7;
    case Qt::Key_F8:
        return KEY_F8;
    case Qt::Key_F9:
        return KEY_F9;
    case Qt::Key_F10:
        return KEY_F10;
    case Qt::Key_F11:
        return KEY_F11;
    case Qt::Key_F12:
        return KEY_F12;
    case Qt::Key_CapsLock:
        return KEY_CAPSLOCK;
    case Qt::Key_ScrollLock:
        return KEY_SCROLLLOCK;
    case Qt::Key_Pause:
        return KEY_PAUSE;
    case Qt::Key_Print:
        return KEY_SYSRQ;
    case Qt::Key_Menu:
        return KEY_MENU;
    case Qt::Key_Minus:
        return KEY_MINUS;
    case Qt::Key_Equal:
        return KEY_EQUAL;
    case Qt::Key_BracketLeft:
        return KEY_LEFTBRACE;
    case Qt::Key_BracketRight:
        return KEY_RIGHTBRACE;
    case Qt::Key_Backslash:
        return KEY_BACKSLASH;
    case Qt::Key_Semicolon:
        return KEY_SEMICOLON;
    case Qt::Key_Apostrophe:
        return KEY_APOSTROPHE;
    case Qt::Key_Comma:
        return KEY_COMMA;
    case Qt::Key_Period:
        return KEY_DOT;
    case Qt::Key_Slash:
        return KEY_SLASH;
    case Qt::Key_QuoteLeft:
        return KEY_GRAVE;
    default:
        return 0;
    }
}

}

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
        portalSession_,
        &PortalSession::clipboardTextChanged,
        this,
        &WaylandDesktopBackend::
            localClipboardTextChanged);

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

void WaylandDesktopBackend::pressLeftAt(
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
        libeiInput_->pressLeftButton();
    }
}

void WaylandDesktopBackend::releaseLeftAt(
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
        libeiInput_->releaseLeftButton();
    }
}

void WaylandDesktopBackend::pressKey(
    int qtKey)
{
    const uint32_t linuxKeyCode =
        linuxKeyCodeForQtKey(qtKey);

    if (linuxKeyCode != 0) {
        libeiInput_->pressKey(
            linuxKeyCode);
    }
}

void WaylandDesktopBackend::releaseKey(
    int qtKey)
{
    const uint32_t linuxKeyCode =
        linuxKeyCodeForQtKey(qtKey);

    if (linuxKeyCode != 0) {
        libeiInput_->releaseKey(
            linuxKeyCode);
    }
}

void WaylandDesktopBackend::applyRemoteClipboardText(
    const QString &text)
{
    portalSession_->setClipboardText(text);
}
