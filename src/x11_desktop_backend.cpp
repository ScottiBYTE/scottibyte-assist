#include "x11_desktop_backend.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <Qt>

#include <algorithm>
#include <set>
#include <unistd.h>

#include <X11/Xatom.h>
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

QList<DesktopBackend::DisplaySource>
X11DesktopBackend::
availableRemoteControlDisplays() const
{
    QList<DisplaySource> sources;

    const QList<QScreen *> screens =
        QGuiApplication::screens();

    QScreen *primary =
        QGuiApplication::primaryScreen();

    for (
        int index = 0;
        index < screens.size();
        ++index
    ) {
        QScreen *screen =
            screens.at(index);

        if (screen == nullptr) {
            continue;
        }

        const QRect geometry =
            screen->geometry();

        QString name =
            screen->name().trimmed();

        if (name.isEmpty()) {
            name =
                QStringLiteral("Display %1")
                    .arg(index + 1);
        }

        QString label =
            QStringLiteral(
                "Display %1 — %2 — %3×%4")
                .arg(index + 1)
                .arg(name)
                .arg(geometry.width())
                .arg(geometry.height());

        if (screen == primary) {
            label +=
                QStringLiteral(" — Primary");
        }

        sources.append(
            {
                QStringLiteral("screen:%1")
                    .arg(index),
                label
            });
    }

    return sources;
}

bool X11DesktopBackend::
setRemoteControlDisplay(
    const QString &displayId)
{
    if (!displayId.startsWith(
            QStringLiteral("screen:"))) {
        return false;
    }

    bool valid = false;

    const int index =
        displayId.mid(
            QStringLiteral("screen:").size())
            .toInt(&valid);

    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (
        !valid ||
        index < 0 ||
        index >= screens.size() ||
        screens.at(index) == nullptr
    ) {
        return false;
    }

    shareSourceId_ = displayId;
    return true;
}

QList<X11DesktopBackend::ShareSource>
X11DesktopBackend::availableShareSources() const
{
    QList<ShareSource> sources;

    sources.append(
        {
            QStringLiteral("desktop"),
            QStringLiteral("Entire Desktop")
        });

    const QList<QScreen *> screens =
        QGuiApplication::screens();

    for (int index = 0;
         index < screens.size();
         ++index) {
        QScreen *screen =
            screens.at(index);

        if (screen == nullptr) {
            continue;
        }

        const QRect geometry =
            screen->geometry();

        QString name =
            screen->name().trimmed();

        if (name.isEmpty()) {
            name =
                QStringLiteral("Monitor %1")
                    .arg(index + 1);
        }

        const QString label =
            QStringLiteral(
                "Monitor %1 — %2 (%3×%4)")
                .arg(index + 1)
                .arg(name)
                .arg(geometry.width())
                .arg(geometry.height());

        sources.append(
            {
                QStringLiteral("screen:%1")
                    .arg(index),
                label
            });
    }

    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        return sources;
    }

    const Window root =
        DefaultRootWindow(display);

    const Atom clientListAtom =
        XInternAtom(
            display,
            "_NET_CLIENT_LIST",
            True);

    const Atom utf8NameAtom =
        XInternAtom(
            display,
            "_NET_WM_NAME",
            True);

    const Atom utf8StringAtom =
        XInternAtom(
            display,
            "UTF8_STRING",
            True);

    const Atom windowPidAtom =
        XInternAtom(
            display,
            "_NET_WM_PID",
            True);

    const Atom windowTypeAtom =
        XInternAtom(
            display,
            "_NET_WM_WINDOW_TYPE",
            True);

    const Atom normalTypeAtom =
        XInternAtom(
            display,
            "_NET_WM_WINDOW_TYPE_NORMAL",
            True);

    const Atom dialogTypeAtom =
        XInternAtom(
            display,
            "_NET_WM_WINDOW_TYPE_DIALOG",
            True);

    if (clientListAtom == None) {
        XCloseDisplay(display);
        return sources;
    }

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char *propertyData = nullptr;

    const int result =
        XGetWindowProperty(
            display,
            root,
            clientListAtom,
            0,
            4096,
            False,
            XA_WINDOW,
            &actualType,
            &actualFormat,
            &itemCount,
            &bytesAfter,
            &propertyData);

    QList<ShareSource> windowSources;

    std::set<QString>
        encounteredWindowLabels;

    const unsigned long currentProcessId =
        static_cast<unsigned long>(
            getpid());

    if (
        result == Success &&
        propertyData != nullptr &&
        actualType == XA_WINDOW &&
        actualFormat == 32) {
        const Window *windows =
            reinterpret_cast<Window *>(
                propertyData);

        for (unsigned long index = 0;
             index < itemCount;
             ++index) {
            const Window candidate =
                windows[index];

            XWindowAttributes attributes{};

            if (
                !XGetWindowAttributes(
                    display,
                    candidate,
                    &attributes) ||
                attributes.map_state !=
                    IsViewable ||
                attributes.override_redirect ||
                attributes.width < 240 ||
                attributes.height < 160) {
                continue;
            }

            bool ownedByAssist = false;

            if (windowPidAtom != None) {
                Atom pidType = None;
                int pidFormat = 0;
                unsigned long pidItems = 0;
                unsigned long pidBytesAfter = 0;
                unsigned char *pidData = nullptr;

                if (
                    XGetWindowProperty(
                        display,
                        candidate,
                        windowPidAtom,
                        0,
                        1,
                        False,
                        XA_CARDINAL,
                        &pidType,
                        &pidFormat,
                        &pidItems,
                        &pidBytesAfter,
                        &pidData) ==
                        Success &&
                    pidData != nullptr &&
                    pidFormat == 32 &&
                    pidItems == 1) {
                    const auto ownerPid =
                        *reinterpret_cast<
                            unsigned long *>(
                                pidData);

                    ownedByAssist =
                        ownerPid ==
                        currentProcessId;

                    XFree(pidData);
                }
            }

            if (ownedByAssist) {
                continue;
            }

            bool usefulWindowType = true;

            if (
                windowTypeAtom != None &&
                normalTypeAtom != None) {
                Atom typeType = None;
                int typeFormat = 0;
                unsigned long typeItems = 0;
                unsigned long typeBytesAfter = 0;
                unsigned char *typeData = nullptr;

                if (
                    XGetWindowProperty(
                        display,
                        candidate,
                        windowTypeAtom,
                        0,
                        16,
                        False,
                        XA_ATOM,
                        &typeType,
                        &typeFormat,
                        &typeItems,
                        &typeBytesAfter,
                        &typeData) ==
                        Success &&
                    typeData != nullptr &&
                    typeFormat == 32 &&
                    typeItems > 0) {
                    usefulWindowType = false;

                    const Atom *types =
                        reinterpret_cast<
                            Atom *>(typeData);

                    for (
                        unsigned long typeIndex = 0;
                        typeIndex < typeItems;
                        ++typeIndex) {
                        if (
                            types[typeIndex] ==
                                normalTypeAtom ||
                            (
                                dialogTypeAtom != None &&
                                types[typeIndex] ==
                                    dialogTypeAtom
                            )) {
                            usefulWindowType = true;
                            break;
                        }
                    }

                    XFree(typeData);
                }
            }

            if (!usefulWindowType) {
                continue;
            }

            QString title;

            if (
                utf8NameAtom != None &&
                utf8StringAtom != None) {
                Atom nameType = None;
                int nameFormat = 0;
                unsigned long nameItems = 0;
                unsigned long nameBytesAfter = 0;
                unsigned char *nameData = nullptr;

                if (
                    XGetWindowProperty(
                        display,
                        candidate,
                        utf8NameAtom,
                        0,
                        1024,
                        False,
                        utf8StringAtom,
                        &nameType,
                        &nameFormat,
                        &nameItems,
                        &nameBytesAfter,
                        &nameData) ==
                        Success &&
                    nameData != nullptr) {
                    title =
                        QString::fromUtf8(
                            reinterpret_cast<char *>(
                                nameData),
                            static_cast<int>(
                                nameItems));

                    XFree(nameData);
                }
            }

            if (title.trimmed().isEmpty()) {
                char *legacyName = nullptr;

                if (
                    XFetchName(
                        display,
                        candidate,
                        &legacyName) &&
                    legacyName != nullptr) {
                    title =
                        QString::fromLocal8Bit(
                            legacyName);

                    XFree(legacyName);
                }
            }

            title = title.trimmed();

            if (title.isEmpty()) {
                continue;
            }

            const QString normalizedTitle =
                title.toLower();

            if (
                normalizedTitle ==
                    QStringLiteral("trash") ||
                normalizedTitle ==
                    QStringLiteral("desktop") ||
                normalizedTitle.startsWith(
                    QStringLiteral(
                        "scottibyte assist"))) {
                continue;
            }

            const QString label =
                QStringLiteral(
                    "Window — %1 (%2×%3)")
                    .arg(title)
                    .arg(attributes.width)
                    .arg(attributes.height);

            if (
                encounteredWindowLabels.contains(
                    label)) {
                continue;
            }

            encounteredWindowLabels.insert(
                label);

            windowSources.append(
                {
                    QStringLiteral("window:%1")
                        .arg(
                            static_cast<qulonglong>(
                                candidate)),
                    label
                });
        }
    }

    if (propertyData != nullptr) {
        XFree(propertyData);
    }

    XCloseDisplay(display);

    std::sort(
        windowSources.begin(),
        windowSources.end(),
        [](
            const ShareSource &left,
            const ShareSource &right)
        {
            return left.label.localeAwareCompare(
                       right.label) < 0;
        });

    sources.append(windowSources);

    return sources;
}

void X11DesktopBackend::setShareSource(
    const QString &sourceId)
{
    if (sourceId.isEmpty()) {
        shareSourceId_ =
            QStringLiteral("desktop");
        return;
    }

    shareSourceId_ = sourceId;
}

QString X11DesktopBackend::shareSource() const
{
    return shareSourceId_;
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

QImage
X11DesktopBackend::captureEntireDesktop() const
{
    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (screens.isEmpty()) {
        return {};
    }

    QRect desktopGeometry;

    for (QScreen *screen : screens) {
        if (screen != nullptr) {
            desktopGeometry =
                desktopGeometry.united(
                    screen->geometry());
        }
    }

    if (desktopGeometry.isEmpty()) {
        return {};
    }

    QImage image(
        desktopGeometry.size(),
        QImage::Format_RGB32);

    image.fill(Qt::black);

    QPainter painter(&image);

    for (QScreen *screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        const QPixmap screenImage =
            screen->grabWindow(0);

        if (screenImage.isNull()) {
            continue;
        }

        const QRect geometry =
            screen->geometry();

        const QRect targetRect(
            geometry.topLeft() -
                desktopGeometry.topLeft(),
            geometry.size());

        painter.drawPixmap(
            targetRect,
            screenImage);
    }

    return image;
}

QImage X11DesktopBackend::captureScreen(
    int screenIndex) const
{
    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (
        screenIndex < 0 ||
        screenIndex >= screens.size() ||
        screens.at(screenIndex) == nullptr) {
        return {};
    }

    return screens.at(screenIndex)
        ->grabWindow(0)
        .toImage();
}

QImage X11DesktopBackend::captureWindow(
    unsigned long windowId) const
{
    if (windowId == 0) {
        return {};
    }

    Display *display =
        XOpenDisplay(nullptr);

    if (display == nullptr) {
        return {};
    }

    const Window window =
        static_cast<Window>(windowId);

    XWindowAttributes attributes{};

    const bool windowExists =
        XGetWindowAttributes(
            display,
            window,
            &attributes) != 0;

    bool windowHidden = false;

    if (windowExists) {
        const Atom stateAtom =
            XInternAtom(
                display,
                "_NET_WM_STATE",
                True);

        const Atom hiddenAtom =
            XInternAtom(
                display,
                "_NET_WM_STATE_HIDDEN",
                True);

        if (
            stateAtom != None &&
            hiddenAtom != None) {
            Atom actualType = None;
            int actualFormat = 0;
            unsigned long itemCount = 0;
            unsigned long bytesAfter = 0;
            unsigned char *propertyData = nullptr;

            if (
                XGetWindowProperty(
                    display,
                    window,
                    stateAtom,
                    0,
                    32,
                    False,
                    XA_ATOM,
                    &actualType,
                    &actualFormat,
                    &itemCount,
                    &bytesAfter,
                    &propertyData) ==
                    Success &&
                propertyData != nullptr &&
                actualFormat == 32) {
                const Atom *states =
                    reinterpret_cast<Atom *>(
                        propertyData);

                for (
                    unsigned long index = 0;
                    index < itemCount;
                    ++index) {
                    if (
                        states[index] ==
                        hiddenAtom) {
                        windowHidden = true;
                        break;
                    }
                }

                XFree(propertyData);
            }
        }
    }

    XCloseDisplay(display);

    if (!windowExists) {
        return {};
    }

    if (
        attributes.map_state != IsViewable ||
        windowHidden) {
        QImage placeholder(
            1280,
            720,
            QImage::Format_RGB32);

        placeholder.fill(
            QColor(
                4,
                19,
                39));

        QPainter painter(
            &placeholder);

        painter.setRenderHint(
            QPainter::Antialiasing,
            true);

        painter.setPen(
            QColor(
                125,
                234,
                255));

        QFont messageFont =
            painter.font();

        messageFont.setPointSize(22);
        messageFont.setBold(true);

        painter.setFont(
            messageFont);

        painter.drawText(
            placeholder.rect(),
            Qt::AlignCenter,
            QStringLiteral(
                "The shared window is minimized.\n"
                "It will reappear when the provider restores it."));

        return placeholder;
    }

    QScreen *screen =
        QGuiApplication::primaryScreen();

    if (screen == nullptr) {
        return {};
    }

    return screen->grabWindow(
        static_cast<WId>(windowId))
        .toImage();
}

void X11DesktopBackend::captureFrame()
{
    QImage image;

    if (
        shareSourceId_ ==
        QStringLiteral("desktop")) {
        image =
            captureEntireDesktop();
    } else if (
        shareSourceId_.startsWith(
            QStringLiteral("screen:"))) {
        bool valid = false;

        const int screenIndex =
            shareSourceId_
                .mid(
                    QStringLiteral(
                        "screen:").size())
                .toInt(&valid);

        if (valid) {
            image =
                captureScreen(
                    screenIndex);
        }
    } else if (
        shareSourceId_.startsWith(
            QStringLiteral("window:"))) {
        bool valid = false;

        const qulonglong windowId =
            shareSourceId_
                .mid(
                    QStringLiteral(
                        "window:").size())
                .toULongLong(
                    &valid);

        if (valid) {
            image =
                captureWindow(
                    static_cast<unsigned long>(
                        windowId));
        }
    }

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
