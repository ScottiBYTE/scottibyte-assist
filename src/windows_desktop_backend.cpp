#include "windows_desktop_backend.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <Qt>

#include <algorithm>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

namespace
{

bool currentWindowsCursorImage(
    QImage &image,
    int &hotspotX,
    int &hotspotY,
    HCURSOR &cursorHandle)
{
    CURSORINFO cursorInfo {};
    cursorInfo.cbSize =
        sizeof(cursorInfo);

    if (!GetCursorInfo(
            &cursorInfo) ||
        !(cursorInfo.flags &
          CURSOR_SHOWING) ||
        cursorInfo.hCursor == nullptr) {
        return false;
    }

    ICONINFO iconInfo {};

    if (!GetIconInfo(
            cursorInfo.hCursor,
            &iconInfo)) {
        return false;
    }

    hotspotX =
        static_cast<int>(
            iconInfo.xHotspot);

    hotspotY =
        static_cast<int>(
            iconInfo.yHotspot);

    BITMAP colorBitmap {};
    BITMAP maskBitmap {};

    int width = 0;
    int height = 0;

    if (iconInfo.hbmColor != nullptr &&
        GetObjectW(
            iconInfo.hbmColor,
            sizeof(colorBitmap),
            &colorBitmap) != 0) {
        width =
            colorBitmap.bmWidth;

        height =
            colorBitmap.bmHeight;
    } else if (
        iconInfo.hbmMask != nullptr &&
        GetObjectW(
            iconInfo.hbmMask,
            sizeof(maskBitmap),
            &maskBitmap) != 0
    ) {
        width =
            maskBitmap.bmWidth;

        height =
            maskBitmap.bmHeight / 2;
    }

    if (width <= 0 ||
        height <= 0) {
        if (iconInfo.hbmColor != nullptr) {
            DeleteObject(
                iconInfo.hbmColor);
        }

        if (iconInfo.hbmMask != nullptr) {
            DeleteObject(
                iconInfo.hbmMask);
        }

        return false;
    }

    QImage cursorImage(
        width,
        height,
        QImage::Format_ARGB32_Premultiplied);

    cursorImage.fill(
        Qt::transparent);

    HDC screenDc =
        GetDC(nullptr);

    HDC memoryDc =
        CreateCompatibleDC(
            screenDc);

    HBITMAP dib =
        CreateCompatibleBitmap(
            screenDc,
            width,
            height);

    HGDIOBJ oldBitmap = nullptr;

    bool success = false;

    if (memoryDc != nullptr &&
        dib != nullptr) {
        oldBitmap =
            SelectObject(
                memoryDc,
                dib);

        RECT rect {
            0,
            0,
            width,
            height
        };

        FillRect(
            memoryDc,
            &rect,
            static_cast<HBRUSH>(
                GetStockObject(
                    BLACK_BRUSH)));

        if (DrawIconEx(
                memoryDc,
                0,
                0,
                cursorInfo.hCursor,
                width,
                height,
                0,
                nullptr,
                DI_NORMAL)) {
            BITMAPINFO bitmapInfo {};
            bitmapInfo.bmiHeader.biSize =
                sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth =
                width;
            bitmapInfo.bmiHeader.biHeight =
                -height;
            bitmapInfo.bmiHeader.biPlanes =
                1;
            bitmapInfo.bmiHeader.biBitCount =
                32;
            bitmapInfo.bmiHeader.biCompression =
                BI_RGB;

            success =
                GetDIBits(
                    memoryDc,
                    dib,
                    0,
                    static_cast<UINT>(
                        height),
                    cursorImage.bits(),
                    &bitmapInfo,
                    DIB_RGB_COLORS) != 0;

            /*
             * Classic Windows monochrome cursors such as
             * the I-beam have no color bitmap. DrawIconEx
             * renders their AND/XOR mask correctly, but a
             * compatible bitmap does not give us a useful
             * alpha channel. Reconstruct transparency by
             * drawing the same cursor over both black and
             * white backgrounds.
             *
             * Leave color cursors completely untouched.
             */
            bool hasVisibleAlpha = false;

            if (success) {
                for (int y = 0;
                     y < height && !hasVisibleAlpha;
                     ++y) {
                    for (int x = 0;
                         x < width;
                         ++x) {
                        if (
                            qAlpha(
                                cursorImage.pixel(
                                    x,
                                    y)) != 0
                        ) {
                            hasVisibleAlpha = true;
                            break;
                        }
                    }
                }
            }

            if (
                success &&
                !hasVisibleAlpha
            ) {
                QImage whiteImage(
                    width,
                    height,
                    QImage::Format_ARGB32_Premultiplied);

                whiteImage.fill(
                    Qt::white);

                FillRect(
                    memoryDc,
                    &rect,
                    static_cast<HBRUSH>(
                        GetStockObject(
                            WHITE_BRUSH)));

                const bool whiteDrawn =
                    DrawIconEx(
                        memoryDc,
                        0,
                        0,
                        cursorInfo.hCursor,
                        width,
                        height,
                        0,
                        nullptr,
                        DI_NORMAL) != FALSE;

                const bool whiteRead =
                    whiteDrawn &&
                    GetDIBits(
                        memoryDc,
                        dib,
                        0,
                        static_cast<UINT>(
                            height),
                        whiteImage.bits(),
                        &bitmapInfo,
                        DIB_RGB_COLORS) != 0;

                if (whiteRead) {
                    /*
                     * Windows monochrome cursors can contain
                     * XOR pixels whose native meaning is
                     * "invert the desktop underneath me."
                     *
                     * A transported ARGB image cannot retain
                     * that operation directly. Mark those
                     * pixels temporarily with alpha 254, then
                     * render them as a black core surrounded
                     * by a one-pixel white outline. This keeps
                     * cursors such as the I-beam visible on
                     * both light and dark remote content.
                     */
                    for (int y = 0; y < height; ++y) {
                        for (int x = 0; x < width; ++x) {
                            const QRgb blackPixel =
                                cursorImage.pixel(x, y);

                            const QRgb whitePixel =
                                whiteImage.pixel(x, y);

                            const bool transparentPixel =
                                qRed(blackPixel) == 0 &&
                                qGreen(blackPixel) == 0 &&
                                qBlue(blackPixel) == 0 &&
                                qRed(whitePixel) == 255 &&
                                qGreen(whitePixel) == 255 &&
                                qBlue(whitePixel) == 255;

                            const bool invertPixel =
                                qRed(blackPixel) == 255 &&
                                qGreen(blackPixel) == 255 &&
                                qBlue(blackPixel) == 255 &&
                                qRed(whitePixel) == 0 &&
                                qGreen(whitePixel) == 0 &&
                                qBlue(whitePixel) == 0;

                            if (transparentPixel) {
                                cursorImage.setPixel(
                                    x,
                                    y,
                                    qRgba(0, 0, 0, 0));
                            } else if (invertPixel) {
                                cursorImage.setPixel(
                                    x,
                                    y,
                                    qRgba(0, 0, 0, 254));
                            } else {
                                cursorImage.setPixel(
                                    x,
                                    y,
                                    qRgba(
                                        qRed(blackPixel),
                                        qGreen(blackPixel),
                                        qBlue(blackPixel),
                                        255));
                            }
                        }
                    }

                    QImage contrastedCursor =
                        cursorImage;

                    for (int y = 0; y < height; ++y) {
                        for (int x = 0; x < width; ++x) {
                            if (
                                qAlpha(
                                    cursorImage.pixel(
                                        x,
                                        y)) != 254
                            ) {
                                continue;
                            }

                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const int outlineX = x + dx;
                                    const int outlineY = y + dy;

                                    if (
                                        outlineX < 0 ||
                                        outlineY < 0 ||
                                        outlineX >= width ||
                                        outlineY >= height
                                    ) {
                                        continue;
                                    }

                                    if (
                                        qAlpha(
                                            cursorImage.pixel(
                                                outlineX,
                                                outlineY)) == 0
                                    ) {
                                        contrastedCursor.setPixel(
                                            outlineX,
                                            outlineY,
                                            qRgba(
                                                255,
                                                255,
                                                255,
                                                255));
                                    }
                                }
                            }

                            contrastedCursor.setPixel(
                                x,
                                y,
                                qRgba(0, 0, 0, 255));
                        }
                    }

                    cursorImage =
                        contrastedCursor;
                }
            }
        }
    }

    if (oldBitmap != nullptr) {
        SelectObject(
            memoryDc,
            oldBitmap);
    }

    if (dib != nullptr) {
        DeleteObject(
            dib);
    }

    if (memoryDc != nullptr) {
        DeleteDC(
            memoryDc);
    }

    if (screenDc != nullptr) {
        ReleaseDC(
            nullptr,
            screenDc);
    }

    if (iconInfo.hbmColor != nullptr) {
        DeleteObject(
            iconInfo.hbmColor);
    }

    if (iconInfo.hbmMask != nullptr) {
        DeleteObject(
            iconInfo.hbmMask);
    }

    if (!success) {
        return false;
    }

    cursorHandle =
        cursorInfo.hCursor;

    image =
        cursorImage;

    return true;
}

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

bool sendAbsoluteMouseMove(
    int desktopX,
    int desktopY)
{
    const int virtualLeft =
        GetSystemMetrics(
            SM_XVIRTUALSCREEN);

    const int virtualTop =
        GetSystemMetrics(
            SM_YVIRTUALSCREEN);

    const int virtualWidth =
        GetSystemMetrics(
            SM_CXVIRTUALSCREEN);

    const int virtualHeight =
        GetSystemMetrics(
            SM_CYVIRTUALSCREEN);

    if (
        virtualWidth <= 1 ||
        virtualHeight <= 1
    ) {
        return false;
    }

    const LONG normalizedX =
        static_cast<LONG>(
            (
                static_cast<long long>(
                    desktopX - virtualLeft) *
                65535
            ) /
            (virtualWidth - 1));

    const LONG normalizedY =
        static_cast<LONG>(
            (
                static_cast<long long>(
                    desktopY - virtualTop) *
                65535
            ) /
            (virtualHeight - 1));

    INPUT input{};
    input.type =
        INPUT_MOUSE;

    input.mi.dx =
        normalizedX;

    input.mi.dy =
        normalizedY;

    input.mi.dwFlags =
        MOUSEEVENTF_MOVE |
        MOUSEEVENTF_ABSOLUTE |
        MOUSEEVENTF_VIRTUALDESK;

    return
        SendInput(
            1,
            &input,
            sizeof(INPUT)) == 1;
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


bool nativeMonitorGeometryForScreen(
    QScreen *screen,
    QRect &geometry)
{
    if (screen == nullptr) {
        return false;
    }

    /*
     * On Windows Qt scales each screen's size for DPI,
     * but preserves its virtual-desktop position.
     *
     * Use that position only to identify the native
     * HMONITOR. All subsequent geometry comes directly
     * from Windows in native virtual-screen coordinates.
     */
    const QRect qtGeometry =
        screen->geometry();

    POINT point{
        qtGeometry.left() + 1,
        qtGeometry.top() + 1
    };

    HMONITOR monitor =
        MonitorFromPoint(
            point,
            MONITOR_DEFAULTTONEAREST);

    if (monitor == nullptr) {
        return false;
    }

    MONITORINFO info{};
    info.cbSize =
        sizeof(info);

    if (!GetMonitorInfoW(
            monitor,
            &info)) {
        return false;
    }

    const int width =
        info.rcMonitor.right -
        info.rcMonitor.left;

    const int height =
        info.rcMonitor.bottom -
        info.rcMonitor.top;

    if (
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    geometry =
        QRect(
            info.rcMonitor.left,
            info.rcMonitor.top,
            width,
            height);

    return true;
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

QList<WindowsDesktopBackend::ShareSource>
WindowsDesktopBackend::
availableShareSources() const
{
    QList<ShareSource> sources;

    sources.append(
        {
            QStringLiteral("desktop"),
            QStringLiteral("Entire Desktop")
        });

    const QList<QScreen *> screens =
        QGuiApplication::screens();

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

        QString name =
            screen->name().trimmed();

        if (name.isEmpty()) {
            name =
                QStringLiteral("Display %1")
                    .arg(index + 1);
        }

        sources.append(
            {
                QStringLiteral("screen:%1")
                    .arg(index),
                QStringLiteral(
                    "Display %1 — %2")
                    .arg(index + 1)
                    .arg(name)
            });
    }

    struct EnumerationContext
    {
        QList<ShareSource> *sources;
    };

    EnumerationContext context {
        &sources
    };

    EnumWindows(
        [](
            HWND hwnd,
            LPARAM parameter) -> BOOL
        {
            auto *context =
                reinterpret_cast<
                    EnumerationContext *>(
                        parameter);

            if (
                context == nullptr ||
                context->sources == nullptr
            ) {
                return TRUE;
            }

            if (!IsWindowVisible(hwnd)) {
                return TRUE;
            }

            if (GetWindow(hwnd, GW_OWNER) != nullptr) {
                return TRUE;
            }

            DWORD cloaked = 0;

            if (
                SUCCEEDED(
                    DwmGetWindowAttribute(
                        hwnd,
                        DWMWA_CLOAKED,
                        &cloaked,
                        sizeof(cloaked))) &&
                cloaked != 0
            ) {
                return TRUE;
            }

            const LONG_PTR exStyle =
                GetWindowLongPtrW(
                    hwnd,
                    GWL_EXSTYLE);

            if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
                return TRUE;
            }

            wchar_t classBuffer[256] {};

            GetClassNameW(
                hwnd,
                classBuffer,
                static_cast<int>(
                    sizeof(classBuffer) /
                    sizeof(classBuffer[0])));

            const QString className =
                QString::fromWCharArray(
                    classBuffer).trimmed();

            if (
                className ==
                    QStringLiteral("Progman") ||
                className ==
                    QStringLiteral("WorkerW")
            ) {
                return TRUE;
            }

            wchar_t title[512] {};

            const int length =
                GetWindowTextW(
                    hwnd,
                    title,
                    static_cast<int>(
                        sizeof(title) /
                        sizeof(title[0])));

            if (length <= 0) {
                return TRUE;
            }

            const QString windowTitle =
                QString::fromWCharArray(
                    title,
                    length).trimmed();

            if (windowTitle.isEmpty()) {
                return TRUE;
            }

            RECT rect {};

            if (!GetWindowRect(
                    hwnd,
                    &rect)) {
                return TRUE;
            }

            if (IsIconic(hwnd)) {
                WINDOWPLACEMENT placement {};
                placement.length =
                    sizeof(placement);

                if (GetWindowPlacement(
                        hwnd,
                        &placement)) {
                    rect =
                        placement.rcNormalPosition;
                }
            }

            const int width =
                rect.right - rect.left;

            const int height =
                rect.bottom - rect.top;

            if (
                width < 240 ||
                height < 160
            ) {
                return TRUE;
            }

            DWORD processId = 0;

            GetWindowThreadProcessId(
                hwnd,
                &processId);

            if (
                processId ==
                GetCurrentProcessId() &&
                windowTitle !=
                    QStringLiteral(
                        "ScottiBYTE Assist — "
                        "Customer Terminal")
            ) {
                return TRUE;
            }

            const quintptr windowId =
                reinterpret_cast<quintptr>(
                    hwnd);

            context->sources->append(
                {
                    QStringLiteral(
                        "window:%1")
                        .arg(
                            static_cast<
                                qulonglong>(
                                    windowId),
                            0,
                            16),
                    QStringLiteral(
                        "Window — %1")
                        .arg(windowTitle)
                });

            return TRUE;
        },
        reinterpret_cast<LPARAM>(
            &context));

    return sources;
}

QString WindowsDesktopBackend::
shareSource() const
{
    return shareSourceId_;
}

bool WindowsDesktopBackend::
setShareSource(
    const QString &sourceId)
{
    if (
        sourceId == QStringLiteral(
            "desktop") ||
        sourceId.startsWith(
            QStringLiteral("screen:")) ||
        sourceId.startsWith(
            QStringLiteral("window:"))
    ) {
        shareSourceId_ = sourceId;

        captureTargetMode_ =
            CaptureTargetMode::ShareSource;

        return true;
    }

    return false;
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

    captureTargetMode_ =
        CaptureTargetMode::RemoteControlDisplay;

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

QImage WindowsDesktopBackend::
captureEntireDesktop() const
{
    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (screens.isEmpty()) {
        return {};
    }

    QRect desktopGeometry;

    for (QScreen *screen : screens) {
        if (screen == nullptr) {
            continue;
        }

        desktopGeometry =
            desktopGeometry.united(
                screen->geometry());
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

        const QPixmap pixmap =
            screen->grabWindow(0);

        if (pixmap.isNull()) {
            continue;
        }

        const QRect geometry =
            screen->geometry();

        const QPoint target =
            geometry.topLeft() -
            desktopGeometry.topLeft();

        painter.drawPixmap(
            target,
            pixmap);
    }

    return image;
}

QImage WindowsDesktopBackend::
captureShareScreen(
    int screenIndex) const
{
    const QList<QScreen *> screens =
        QGuiApplication::screens();

    if (
        screenIndex < 0 ||
        screenIndex >= screens.size() ||
        screens.at(screenIndex) == nullptr
    ) {
        return {};
    }

    return screens.at(screenIndex)
        ->grabWindow(0)
        .toImage()
        .convertToFormat(
            QImage::Format_RGB32);
}

QImage WindowsDesktopBackend::
captureWindow(
    quintptr windowId) const
{
    if (windowId == 0) {
        return {};
    }

    HWND hwnd =
        reinterpret_cast<HWND>(
            windowId);

    if (!IsWindow(hwnd)) {
        return {};
    }

    RECT rect {};

    if (!GetWindowRect(
            hwnd,
            &rect)) {
        return {};
    }

    const bool minimized =
        IsIconic(hwnd);

    if (minimized) {
        WINDOWPLACEMENT placement {};
        placement.length =
            sizeof(placement);

        if (GetWindowPlacement(
                hwnd,
                &placement)) {
            rect =
                placement.rcNormalPosition;
        }
    }

    const int width =
        rect.right - rect.left;

    const int height =
        rect.bottom - rect.top;

    if (
        width <= 0 ||
        height <= 0
    ) {
        return {};
    }

    if (minimized) {
        QImage image(
            width,
            height,
            QImage::Format_RGB32);

        image.fill(
            Qt::black);

        QPainter painter(
            &image);

        painter.setPen(
            Qt::white);

        QFont font =
            painter.font();

        font.setPixelSize(22);
        font.setBold(true);

        painter.setFont(font);

        painter.drawText(
            image.rect().adjusted(
                24,
                24,
                -24,
                -24),
            Qt::AlignCenter |
                Qt::TextWordWrap,
            QStringLiteral(
                "The shared window is minimized.\n"
                "It will reappear when the provider restores it."));

        return image;
    }

    HDC windowDc =
        GetWindowDC(hwnd);

    if (windowDc == nullptr) {
        return {};
    }

    HDC memoryDc =
        CreateCompatibleDC(
            windowDc);

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            windowDc,
            width,
            height);

    HGDIOBJ oldBitmap = nullptr;

    if (
        memoryDc != nullptr &&
        bitmap != nullptr
    ) {
        oldBitmap =
            SelectObject(
                memoryDc,
                bitmap);
    }

    bool captured = false;

    if (
        memoryDc != nullptr &&
        bitmap != nullptr
    ) {
        constexpr UINT
            printWindowFullContent = 0x00000002;

        captured =
            PrintWindow(
                hwnd,
                memoryDc,
                printWindowFullContent) != FALSE;

        if (!captured) {
            captured =
                BitBlt(
                    memoryDc,
                    0,
                    0,
                    width,
                    height,
                    windowDc,
                    0,
                    0,
                    SRCCOPY |
                    CAPTUREBLT) != FALSE;
        }
    }

    QImage image;

    if (captured) {
        image =
            QImage(
                width,
                height,
                QImage::Format_ARGB32);

        BITMAPINFO bitmapInfo {};
        bitmapInfo.bmiHeader.biSize =
            sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth =
            width;
        bitmapInfo.bmiHeader.biHeight =
            -height;
        bitmapInfo.bmiHeader.biPlanes =
            1;
        bitmapInfo.bmiHeader.biBitCount =
            32;
        bitmapInfo.bmiHeader.biCompression =
            BI_RGB;

        if (
            GetDIBits(
                memoryDc,
                bitmap,
                0,
                static_cast<UINT>(
                    height),
                image.bits(),
                &bitmapInfo,
                DIB_RGB_COLORS) == 0
        ) {
            image = {};
        }
    }

    if (oldBitmap != nullptr) {
        SelectObject(
            memoryDc,
            oldBitmap);
    }

    if (bitmap != nullptr) {
        DeleteObject(bitmap);
    }

    if (memoryDc != nullptr) {
        DeleteDC(memoryDc);
    }

    ReleaseDC(
        hwnd,
        windowDc);

    if (!image.isNull()) {
        image =
            image.convertToFormat(
                QImage::Format_RGB32);
    }

    return image;
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
    lastCursorHandle_ = nullptr;

    emit statusChanged(
        QStringLiteral(
            "Windows desktop capture stopped."));
}

void WindowsDesktopBackend::captureFrame()
{
    if (!running_) {
        return;
    }

    if (
        captureTargetMode_ ==
        CaptureTargetMode::ShareSource
    ) {
        QImage frame;

        if (
            shareSourceId_ ==
            QStringLiteral("desktop")
        ) {
            frame =
                captureEntireDesktop();
        } else if (
            shareSourceId_.startsWith(
                QStringLiteral("screen:"))
        ) {
            bool valid = false;

            const int screenIndex =
                shareSourceId_
                    .mid(
                        QStringLiteral(
                            "screen:").size())
                    .toInt(
                        &valid);

            if (valid) {
                frame =
                    captureShareScreen(
                        screenIndex);
            }
        } else if (
            shareSourceId_.startsWith(
                QStringLiteral("window:"))
        ) {
            bool valid = false;

            const qulonglong value =
                shareSourceId_
                    .mid(
                        QStringLiteral(
                            "window:").size())
                    .toULongLong(
                        &valid,
                        16);

            if (valid) {
                frame =
                    captureWindow(
                        static_cast<quintptr>(
                            value));
            }
        }

        if (frame.isNull()) {
            emit errorOccurred(
                QStringLiteral(
                    "The selected Windows share source "
                    "could not be captured."));
            return;
        }

        frameWidth_ = frame.width();
        frameHeight_ = frame.height();

        emit cursorPositionChanged(
            -1,
            -1);

        emit frameReady(frame);
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

    QRect nativeGeometry;

    if (!nativeMonitorGeometryForScreen(
            screen,
            nativeGeometry)) {
        emit cursorPositionChanged(
            -1,
            -1);

        emit frameReady(frame);
        return;
    }

    POINT cursorPoint{};

    if (
        GetPhysicalCursorPos(
            &cursorPoint) &&
        cursorPoint.x >=
            nativeGeometry.left() &&
        cursorPoint.x <
            nativeGeometry.left() +
            nativeGeometry.width() &&
        cursorPoint.y >=
            nativeGeometry.top() &&
        cursorPoint.y <
            nativeGeometry.top() +
            nativeGeometry.height()
    ) {
        const int relativeX =
            cursorPoint.x -
            nativeGeometry.left();

        const int relativeY =
            cursorPoint.y -
            nativeGeometry.top();

        int frameX = 0;
        int frameY = 0;

        if (
            nativeGeometry.width() > 1 &&
            frameWidth_ > 1
        ) {
            frameX =
                relativeX *
                (frameWidth_ - 1) /
                (nativeGeometry.width() - 1);
        }

        if (
            nativeGeometry.height() > 1 &&
            frameHeight_ > 1
        ) {
            frameY =
                relativeY *
                (frameHeight_ - 1) /
                (nativeGeometry.height() - 1);
        }

        emit cursorPositionChanged(
            std::clamp(
                frameX,
                0,
                frameWidth_ - 1),
            std::clamp(
                frameY,
                0,
                frameHeight_ - 1));
    } else {
        emit cursorPositionChanged(
            -1,
            -1);
    }

    QImage cursorImage;
    int cursorHotspotX = 0;
    int cursorHotspotY = 0;
    HCURSOR cursorHandle = nullptr;

    if (currentWindowsCursorImage(
            cursorImage,
            cursorHotspotX,
            cursorHotspotY,
            cursorHandle)) {
        const void *cursorIdentity =
            static_cast<void *>(
                cursorHandle);

        if (cursorIdentity !=
            lastCursorHandle_) {
            lastCursorHandle_ =
                const_cast<void *>(
                    cursorIdentity);

            emit cursorImageChanged(
                cursorImage,
                cursorHotspotX,
                cursorHotspotY);
        }
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

    QRect nativeGeometry;

    if (!nativeMonitorGeometryForScreen(
            screen,
            nativeGeometry)) {
        return false;
    }

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

    desktopX =
        nativeGeometry.left();

    desktopY =
        nativeGeometry.top();

    if (
        frameWidth_ > 1 &&
        nativeGeometry.width() > 1
    ) {
        desktopX +=
            boundedX *
            (nativeGeometry.width() - 1) /
            (frameWidth_ - 1);
    }

    if (
        frameHeight_ > 1 &&
        nativeGeometry.height() > 1
    ) {
        desktopY +=
            boundedY *
            (nativeGeometry.height() - 1) /
            (frameHeight_ - 1);
    }

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

    if (!sendAbsoluteMouseMove(
            desktopX,
            desktopY)) {
        SetPhysicalCursorPos(
            desktopX,
            desktopY);
    }
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

void WindowsDesktopBackend::scrollWheel(
    int delta)
{
    if (delta == 0) {
        return;
    }

    const std::string command =
        "WHEEL " +
        std::to_string(delta);

    /*
     * Elevated applications and secure/elevated input
     * use the same broker as pointer, button and keyboard
     * input.
     */
    if (sendElevatedBrokerCommand(
            command)) {
        return;
    }

    INPUT input{};
    input.type =
        INPUT_MOUSE;

    input.mi.dwFlags =
        MOUSEEVENTF_WHEEL;

    input.mi.mouseData =
        static_cast<DWORD>(
            delta);

    SendInput(
        1,
        &input,
        sizeof(INPUT));
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
