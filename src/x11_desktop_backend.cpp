#include "x11_desktop_backend.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <Qt>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

X11DesktopBackend::X11DesktopBackend(
    QObject *parent)
    : DesktopBackend(parent),
      captureTimer_(new QTimer(this))
{
    captureTimer_->setInterval(100);

    connect(
        captureTimer_,
        &QTimer::timeout,
        this,
        &X11DesktopBackend::captureFrame);
}

bool X11DesktopBackend::isSupported() const
{
    return
        QGuiApplication::platformName()
            .contains(
                QStringLiteral("xcb"),
                Qt::CaseInsensitive);
}

void X11DesktopBackend::start()
{
    if (!isSupported()) {
        emit errorOccurred(
            QStringLiteral(
                "The X11 desktop backend is unavailable."));
        return;
    }

    captureTimer_->start();

    emit statusChanged(
        QStringLiteral(
            "X11 desktop sharing started."));
}

void X11DesktopBackend::stop()
{
    captureTimer_->stop();

    emit statusChanged(
        QStringLiteral(
            "X11 desktop sharing stopped."));
}

void X11DesktopBackend::captureFrame()
{
    QScreen *screen =
        QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        return;
    }

    QImage image =
        screen->grabWindow(0)
            .toImage();

    if (!image.isNull()) {
        emit frameReady(image);
    }
}

void X11DesktopBackend::movePointerTo(
    int x,
    int y)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not access the X11 display."));
        return;
    }

    XTestFakeMotionEvent(
        display,
        -1,
        x,
        y,
        CurrentTime);

    XFlush(display);
    XCloseDisplay(display);
}

void X11DesktopBackend::clickLeftAt(
    int x,
    int y)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not access the X11 display."));
        return;
    }

    XTestFakeMotionEvent(
        display,
        -1,
        x,
        y,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        1,
        True,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        1,
        False,
        CurrentTime);

    XFlush(display);
    XCloseDisplay(display);
}

void X11DesktopBackend::clickRightAt(
    int x,
    int y)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not access the X11 display."));
        return;
    }

    XTestFakeMotionEvent(
        display,
        -1,
        x,
        y,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        3,
        True,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        3,
        False,
        CurrentTime);

    XFlush(display);
    XCloseDisplay(display);
}

void X11DesktopBackend::pressLeftAt(
    int x,
    int y)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not access the X11 display."));
        return;
    }

    XTestFakeMotionEvent(
        display,
        -1,
        x,
        y,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        1,
        True,
        CurrentTime);

    XFlush(display);
    XCloseDisplay(display);
}

void X11DesktopBackend::releaseLeftAt(
    int x,
    int y)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not access the X11 display."));
        return;
    }

    XTestFakeMotionEvent(
        display,
        -1,
        x,
        y,
        CurrentTime);

    XTestFakeButtonEvent(
        display,
        1,
        False,
        CurrentTime);

    XFlush(display);
    XCloseDisplay(display);
}

namespace
{

KeySym x11KeySymForQtKey(
    int qtKey)
{
    if (qtKey >= Qt::Key_A &&
        qtKey <= Qt::Key_Z) {
        return XK_a +
            (qtKey - Qt::Key_A);
    }

    if (qtKey >= Qt::Key_0 &&
        qtKey <= Qt::Key_9) {
        return XK_0 +
            (qtKey - Qt::Key_0);
    }

    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return XK_Return;
    case Qt::Key_Backspace:
        return XK_BackSpace;
    case Qt::Key_Tab:
        return XK_Tab;
    case Qt::Key_Space:
        return XK_space;
    case Qt::Key_Escape:
        return XK_Escape;
    case Qt::Key_Left:
        return XK_Left;
    case Qt::Key_Right:
        return XK_Right;
    case Qt::Key_Up:
        return XK_Up;
    case Qt::Key_Down:
        return XK_Down;
    case Qt::Key_Home:
        return XK_Home;
    case Qt::Key_End:
        return XK_End;
    case Qt::Key_PageUp:
        return XK_Page_Up;
    case Qt::Key_PageDown:
        return XK_Page_Down;
    case Qt::Key_Insert:
        return XK_Insert;
    case Qt::Key_Delete:
        return XK_Delete;
    case Qt::Key_Shift:
        return XK_Shift_L;
    case Qt::Key_Control:
        return XK_Control_L;
    case Qt::Key_Alt:
        return XK_Alt_L;
    case Qt::Key_Meta:
        return XK_Super_L;
    case Qt::Key_F1:
        return XK_F1;
    case Qt::Key_F2:
        return XK_F2;
    case Qt::Key_F3:
        return XK_F3;
    case Qt::Key_F4:
        return XK_F4;
    case Qt::Key_F5:
        return XK_F5;
    case Qt::Key_F6:
        return XK_F6;
    case Qt::Key_F7:
        return XK_F7;
    case Qt::Key_F8:
        return XK_F8;
    case Qt::Key_F9:
        return XK_F9;
    case Qt::Key_F10:
        return XK_F10;
    case Qt::Key_F11:
        return XK_F11;
    case Qt::Key_F12:
        return XK_F12;
    case Qt::Key_CapsLock:
        return XK_Caps_Lock;
    case Qt::Key_ScrollLock:
        return XK_Scroll_Lock;
    case Qt::Key_Pause:
        return XK_Pause;
    case Qt::Key_Print:
        return XK_Print;
    case Qt::Key_Menu:
        return XK_Menu;
    case Qt::Key_Minus:
        return XK_minus;
    case Qt::Key_Equal:
        return XK_equal;
    case Qt::Key_BracketLeft:
        return XK_bracketleft;
    case Qt::Key_BracketRight:
        return XK_bracketright;
    case Qt::Key_Backslash:
        return XK_backslash;
    case Qt::Key_Semicolon:
        return XK_semicolon;
    case Qt::Key_Apostrophe:
        return XK_apostrophe;
    case Qt::Key_Comma:
        return XK_comma;
    case Qt::Key_Period:
        return XK_period;
    case Qt::Key_Slash:
        return XK_slash;
    case Qt::Key_QuoteLeft:
        return XK_grave;
    default:
        return NoSymbol;
    }
}

}

void X11DesktopBackend::pressKey(
    int qtKey)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        return;
    }

    const KeySym keySym =
        x11KeySymForQtKey(qtKey);

    if (keySym != NoSymbol) {
        const KeyCode keyCode =
            XKeysymToKeycode(
                display,
                keySym);

        if (keyCode != 0) {
            XTestFakeKeyEvent(
                display,
                keyCode,
                True,
                CurrentTime);

            XFlush(display);
        }
    }

    XCloseDisplay(display);
}

void X11DesktopBackend::releaseKey(
    int qtKey)
{
    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        return;
    }

    const KeySym keySym =
        x11KeySymForQtKey(qtKey);

    if (keySym != NoSymbol) {
        const KeyCode keyCode =
            XKeysymToKeycode(
                display,
                keySym);

        if (keyCode != 0) {
            XTestFakeKeyEvent(
                display,
                keyCode,
                False,
                CurrentTime);

            XFlush(display);
        }
    }

    XCloseDisplay(display);
}
