#include "windows_desktop_backend.h"

WindowsDesktopBackend::WindowsDesktopBackend(
    QObject *parent)
    : DesktopBackend(parent)
{
}

bool WindowsDesktopBackend::isSupported() const
{
    return false;
}

void WindowsDesktopBackend::start()
{
    emit errorOccurred(
        QStringLiteral(
            "Windows desktop capture is not "
            "implemented yet."));
}

void WindowsDesktopBackend::stop()
{
}

void WindowsDesktopBackend::movePointerTo(
    int,
    int)
{
}

void WindowsDesktopBackend::clickLeftAt(
    int,
    int)
{
}

void WindowsDesktopBackend::pressLeftAt(
    int,
    int)
{
}

void WindowsDesktopBackend::releaseLeftAt(
    int,
    int)
{
}

void WindowsDesktopBackend::clickRightAt(
    int,
    int)
{
}

void WindowsDesktopBackend::pressKey(
    int)
{
}

void WindowsDesktopBackend::releaseKey(
    int)
{
}
