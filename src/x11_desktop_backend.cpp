#include "x11_desktop_backend.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>

#include <X11/Xlib.h>
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
