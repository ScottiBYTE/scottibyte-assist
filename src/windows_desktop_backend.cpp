#include "windows_desktop_backend.h"

#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <Qt>

#include <algorithm>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{

WORD virtualKeyForQtKey(
    int qtKey)
{
    if (qtKey >= Qt::Key_A &&
        qtKey <= Qt::Key_Z) {
        return static_cast<WORD>(
            'A' + (qtKey - Qt::Key_A));
    }

    if (qtKey >= Qt::Key_0 &&
        qtKey <= Qt::Key_9) {
        return static_cast<WORD>(
            '0' + (qtKey - Qt::Key_0));
    }

    if (qtKey >= Qt::Key_F1 &&
        qtKey <= Qt::Key_F12) {
        return static_cast<WORD>(
            VK_F1 + (qtKey - Qt::Key_F1));
    }

    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Backspace:
        return VK_BACK;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Left:
        return VK_LEFT;
    case Qt::Key_Right:
        return VK_RIGHT;
    case Qt::Key_Up:
        return VK_UP;
    case Qt::Key_Down:
        return VK_DOWN;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    case Qt::Key_PageUp:
        return VK_PRIOR;
    case Qt::Key_PageDown:
        return VK_NEXT;
    case Qt::Key_Insert:
        return VK_INSERT;
    case Qt::Key_Delete:
        return VK_DELETE;
    case Qt::Key_Shift:
        return VK_LSHIFT;
    case Qt::Key_Control:
        return VK_LCONTROL;
    case Qt::Key_Alt:
        return VK_LMENU;
    case Qt::Key_Meta:
        return VK_LWIN;
    case Qt::Key_CapsLock:
        return VK_CAPITAL;
    case Qt::Key_ScrollLock:
        return VK_SCROLL;
    case Qt::Key_Pause:
        return VK_PAUSE;
    case Qt::Key_Print:
        return VK_SNAPSHOT;
    case Qt::Key_Menu:
        return VK_APPS;
    case Qt::Key_Minus:
        return VK_OEM_MINUS;
    case Qt::Key_Equal:
        return VK_OEM_PLUS;
    case Qt::Key_BracketLeft:
        return VK_OEM_4;
    case Qt::Key_BracketRight:
        return VK_OEM_6;
    case Qt::Key_Backslash:
        return VK_OEM_5;
    case Qt::Key_Semicolon:
        return VK_OEM_1;
    case Qt::Key_Apostrophe:
        return VK_OEM_7;
    case Qt::Key_Comma:
        return VK_OEM_COMMA;
    case Qt::Key_Period:
        return VK_OEM_PERIOD;
    case Qt::Key_Slash:
        return VK_OEM_2;
    case Qt::Key_QuoteLeft:
        return VK_OEM_3;
    default:
        return 0;
    }
}

bool isExtendedVirtualKey(
    WORD virtualKey)
{
    switch (virtualKey) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

void sendMouseButton(
    DWORD flags)
{
    INPUT input {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;

    SendInput(
        1,
        &input,
        sizeof(INPUT));
}

HANDLE openElevatedBrokerPipe()
{
    constexpr wchar_t pipeName[] =
        L"\\\\.\\pipe\\ScottiBYTEAssistElevatedInput";

    return
        CreateFileW(
            pipeName,
            GENERIC_READ |
                GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
}

bool requestElevatedBrokerLaunch()
{
    constexpr wchar_t servicePipeName[] =
        L"\\\\.\\pipe\\ScottiBYTEAssistPrivileged";

    HANDLE pipe =
        CreateFileW(
            servicePipeName,
            GENERIC_READ |
                GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

    if (pipe ==
        INVALID_HANDLE_VALUE) {
        return false;
    }

    constexpr char request[] =
        "LAUNCH_ELEVATED_HELPER";

    DWORD bytesWritten = 0;

    if (!WriteFile(
            pipe,
            request,
            sizeof(request) - 1,
            &bytesWritten,
            nullptr)) {
        CloseHandle(pipe);
        return false;
    }

    char response[256]{};
    DWORD bytesRead = 0;

    const BOOL readResult =
        ReadFile(
            pipe,
            response,
            sizeof(response) - 1,
            &bytesRead,
            nullptr);

    CloseHandle(pipe);

    if (!readResult) {
        return false;
    }

    const std::string reply(
        response,
        response + bytesRead);

    return
        reply.rfind(
            "ELEVATED_HELPER_LAUNCHED",
            0) == 0;
}

bool sendElevatedBrokerCommand(
    const std::string &command)
{
    HANDLE pipe =
        openElevatedBrokerPipe();

    if (pipe ==
        INVALID_HANDLE_VALUE) {
        /*
         * Avoid repeatedly asking the service to
         * launch a helper for every mouse-move event
         * when the service is unavailable.
         */
        static ULONGLONG lastLaunchAttempt = 0;

        const ULONGLONG now =
            GetTickCount64();

        const bool mayAttemptLaunch =
            lastLaunchAttempt == 0 ||
            now - lastLaunchAttempt >= 5000;

        if (!mayAttemptLaunch) {
            return false;
        }

        lastLaunchAttempt =
            now;

        if (!requestElevatedBrokerLaunch()) {
            return false;
        }

        /*
         * CreateProcessAsUser() returns before the
         * helper necessarily has its named pipe ready.
         * Give it up to roughly one second.
         */
        for (
            int attempt = 0;
            attempt < 40;
            ++attempt
        ) {
            Sleep(25);

            pipe =
                openElevatedBrokerPipe();

            if (pipe !=
                INVALID_HANDLE_VALUE) {
                break;
            }
        }

        if (pipe ==
            INVALID_HANDLE_VALUE) {
            return false;
        }
    }

    DWORD bytesWritten = 0;

    if (!WriteFile(
            pipe,
            command.data(),
            static_cast<DWORD>(
                command.size()),
            &bytesWritten,
            nullptr)) {
        CloseHandle(pipe);
        return false;
    }

    char response[64]{};
    DWORD bytesRead = 0;

    const BOOL readResult =
        ReadFile(
            pipe,
            response,
            sizeof(response) - 1,
            &bytesRead,
            nullptr);

    CloseHandle(pipe);

    if (!readResult) {
        return false;
    }

    return
        std::string(
            response,
            response + bytesRead) ==
        "OK";
}

void sendVirtualKey(
    WORD virtualKey,
    bool pressed)
{
    if (virtualKey == 0) {
        return;
    }

    INPUT input {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = virtualKey;

    if (isExtendedVirtualKey(
            virtualKey)) {
        input.ki.dwFlags |=
            KEYEVENTF_EXTENDEDKEY;
    }

    if (!pressed) {
        input.ki.dwFlags |=
            KEYEVENTF_KEYUP;
    }

    SendInput(
        1,
        &input,
        sizeof(INPUT));
}


}

WindowsDesktopBackend::WindowsDesktopBackend(
    QObject *parent)
    : DesktopBackend(parent)
{
    captureTimer_.setInterval(33);
    captureTimer_.setTimerType(
        Qt::PreciseTimer);

    connect(
        &captureTimer_,
        &QTimer::timeout,
        this,
        &WindowsDesktopBackend::captureFrame);
}

bool WindowsDesktopBackend::isSupported() const
{
    return QGuiApplication::primaryScreen()
        != nullptr;
}

QList<DesktopBackend::DisplaySource>
WindowsDesktopBackend::
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

bool WindowsDesktopBackend::
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

    selectedScreenIndex_ = index;
    return true;
}

QScreen *WindowsDesktopBackend::
selectedScreen() const
{
    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (
        selectedScreenIndex_ >= 0 &&
        selectedScreenIndex_ <
            screens.size() &&
        screens.at(
            selectedScreenIndex_) != nullptr
    ) {
        return screens.at(
            selectedScreenIndex_);
    }

    return QGuiApplication::primaryScreen();
}

void WindowsDesktopBackend::start()
{
    if (running_) {
        return;
    }

    if (!isSupported()) {
        emit errorOccurred(
            QStringLiteral(
                "No Windows display is available "
                "for capture."));
        return;
    }

    running_ = true;

    emit statusChanged(
        QStringLiteral(
            "Windows desktop capture started."));

    captureFrame();
    captureTimer_.start();
}

void WindowsDesktopBackend::stop()
{
    if (!running_) {
        return;
    }

    captureTimer_.stop();
    running_ = false;
    frameWidth_ = 0;
    frameHeight_ = 0;

    emit statusChanged(
        QStringLiteral(
            "Windows desktop capture stopped."));
}

void WindowsDesktopBackend::captureFrame()
{
    if (!running_) {
        return;
    }

    QScreen *screen =
        selectedScreen();

    if (screen == nullptr) {
        stop();

        emit errorOccurred(
            QStringLiteral(
                "The Windows display is no longer "
                "available."));
        return;
    }

    const QPixmap screenshot =
        screen->grabWindow(0);

    if (screenshot.isNull()) {
        emit errorOccurred(
            QStringLiteral(
                "Windows desktop capture returned "
                "an empty frame."));
        return;
    }

    const QImage frame =
        screenshot.toImage()
            .convertToFormat(
                QImage::Format_RGB32);

    if (frame.isNull()) {
        return;
    }

    frameWidth_ = frame.width();
    frameHeight_ = frame.height();

    const QRect screenGeometry =
        screen->geometry();

    POINT cursorPoint{};

    const int frameLeft =
        screenGeometry.left();

    const int frameTop =
        screenGeometry.top();

    const int frameRight =
        frameLeft +
        frameWidth_;

    const int frameBottom =
        frameTop +
        frameHeight_;

    if (
        GetCursorPos(&cursorPoint) &&
        cursorPoint.x >= frameLeft &&
        cursorPoint.x < frameRight &&
        cursorPoint.y >= frameTop &&
        cursorPoint.y < frameBottom
    ) {
        emit cursorPositionChanged(
            cursorPoint.x - frameLeft,
            cursorPoint.y - frameTop);
    }

    emit frameReady(frame);
}

bool WindowsDesktopBackend::desktopPointForFramePoint(
    int x,
    int y,
    int &desktopX,
    int &desktopY) const
{
    if (
        frameWidth_ <= 0 ||
        frameHeight_ <= 0
    ) {
        return false;
    }

    QScreen *screen =
        selectedScreen();

    if (screen == nullptr) {
        return false;
    }

    const QRect screenGeometry =
        screen->geometry();

    const int boundedX =
        std::clamp(
            x,
            0,
            frameWidth_ - 1);

    const int boundedY =
        std::clamp(
            y,
            0,
            frameHeight_ - 1);

    /*
     * On Windows Qt keeps QScreen's virtual-desktop
     * position in the native desktop coordinate system,
     * while grabWindow() returns the monitor's raw
     * device pixels.
     *
     * The remote x/y values are coordinates in that
     * captured frame, so no DPI conversion is needed.
     */
    desktopX =
        screenGeometry.left() +
        boundedX;

    desktopY =
        screenGeometry.top() +
        boundedY;

    return true;
}

void WindowsDesktopBackend::movePointerTo(
    int x,
    int y)
{
    int desktopX = 0;
    int desktopY = 0;

    if (!desktopPointForFramePoint(
            x,
            y,
            desktopX,
            desktopY)) {
        return;
    }

    const std::string command =
        "MOVE " +
        std::to_string(desktopX) +
        " " +
        std::to_string(desktopY);

    if (sendElevatedBrokerCommand(
            command)) {
        return;
    }

    SetCursorPos(
        desktopX,
        desktopY);
}

void WindowsDesktopBackend::clickLeftAt(
    int x,
    int y)
{
    int desktopX = 0;
    int desktopY = 0;

    if (
        desktopPointForFramePoint(
            x,
            y,
            desktopX,
            desktopY)
    ) {
        const std::string command =
            "CLICKAT " +
            std::to_string(desktopX) +
            " " +
            std::to_string(desktopY);

        if (sendElevatedBrokerCommand(
                command)) {
            return;
        }
    }

    movePointerTo(
        x,
        y);

    sendMouseButton(
        MOUSEEVENTF_LEFTDOWN);

    sendMouseButton(
        MOUSEEVENTF_LEFTUP);
}

void WindowsDesktopBackend::pressLeftAt(
    int x,
    int y)
{
    int desktopX = 0;
    int desktopY = 0;

    if (
        desktopPointForFramePoint(
            x,
            y,
            desktopX,
            desktopY)
    ) {
        const std::string command =
            "LDOWNAT " +
            std::to_string(desktopX) +
            " " +
            std::to_string(desktopY);

        if (sendElevatedBrokerCommand(
                command)) {
            return;
        }
    }

    movePointerTo(
        x,
        y);

    if (sendElevatedBrokerCommand(
            "LDOWN")) {
        return;
    }

    sendMouseButton(
        MOUSEEVENTF_LEFTDOWN);
}

void WindowsDesktopBackend::releaseLeftAt(
    int x,
    int y)
{
    int desktopX = 0;
    int desktopY = 0;

    if (
        desktopPointForFramePoint(
            x,
            y,
            desktopX,
            desktopY)
    ) {
        const std::string command =
            "LUPAT " +
            std::to_string(desktopX) +
            " " +
            std::to_string(desktopY);

        if (sendElevatedBrokerCommand(
                command)) {
            return;
        }
    }

    movePointerTo(
        x,
        y);

    sendMouseButton(
        MOUSEEVENTF_LEFTUP);
}

void WindowsDesktopBackend::clickRightAt(
    int x,
    int y)
{
    movePointerTo(
        x,
        y);

    if (
        sendElevatedBrokerCommand(
            "RDOWN") &&
        sendElevatedBrokerCommand(
            "RUP")
    ) {
        return;
    }

    sendMouseButton(
        MOUSEEVENTF_RIGHTDOWN);

    sendMouseButton(
        MOUSEEVENTF_RIGHTUP);
}

void WindowsDesktopBackend::pressKey(
    int qtKey)
{
    const WORD virtualKey =
        virtualKeyForQtKey(
            qtKey);

    if (virtualKey == 0) {
        return;
    }

    const std::string command =
        "KEYDOWN " +
        std::to_string(
            static_cast<unsigned int>(
                virtualKey));

    if (sendElevatedBrokerCommand(
            command)) {
        return;
    }

    sendVirtualKey(
        virtualKey,
        true);
}

void WindowsDesktopBackend::releaseKey(
    int qtKey)
{
    const WORD virtualKey =
        virtualKeyForQtKey(
            qtKey);

    if (virtualKey == 0) {
        return;
    }

    const std::string command =
        "KEYUP " +
        std::to_string(
            static_cast<unsigned int>(
                virtualKey));

    if (sendElevatedBrokerCommand(
            command)) {
        return;
    }

    sendVirtualKey(
        virtualKey,
        false);
}
