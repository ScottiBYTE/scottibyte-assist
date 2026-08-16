#include <windows.h>

#include <fstream>
#include <string>

namespace
{

std::string wideToUtf8(
    const std::wstring &value)
{
    if (value.empty()) {
        return {};
    }

    const int length =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            static_cast<int>(
                value.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

    if (length <= 0) {
        return {};
    }

    std::string result(
        static_cast<std::size_t>(
            length),
        '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(
            value.size()),
        result.data(),
        length,
        nullptr,
        nullptr);

    return result;
}

std::string winlogonDesktopStatus()
{
    HDESK desktop =
        OpenDesktopW(
            L"Winlogon",
            0,
            FALSE,
            DESKTOP_READOBJECTS |
                DESKTOP_WRITEOBJECTS |
                DESKTOP_SWITCHDESKTOP);

    if (desktop == nullptr) {
        return
            "UNAVAILABLE ERROR=" +
            std::to_string(
                GetLastError());
    }

    DWORD requiredBytes = 0;

    GetUserObjectInformationW(
        desktop,
        UOI_NAME,
        nullptr,
        0,
        &requiredBytes);

    std::string result =
        "OPEN";

    if (requiredBytes > 0) {
        std::wstring name(
            requiredBytes /
                sizeof(wchar_t),
            L'\0');

        if (GetUserObjectInformationW(
                desktop,
                UOI_NAME,
                name.data(),
                requiredBytes,
                &requiredBytes)) {
            while (
                !name.empty() &&
                name.back() == L'\0'
            ) {
                name.pop_back();
            }

            result +=
                " NAME=" +
                wideToUtf8(name);
        } else {
            result +=
                " NAME_ERROR=" +
                std::to_string(
                    GetLastError());
        }
    }

    CloseDesktop(desktop);

    return result;
}

bool writeBitmapFile(
    const wchar_t *path,
    HBITMAP bitmap,
    int width,
    int height)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize =
        sizeof(BITMAPINFOHEADER);

    info.bmiHeader.biWidth =
        width;

    info.bmiHeader.biHeight =
        -height;

    info.bmiHeader.biPlanes =
        1;

    info.bmiHeader.biBitCount =
        32;

    info.bmiHeader.biCompression =
        BI_RGB;

    const DWORD imageSize =
        static_cast<DWORD>(
            width *
            height *
            4);

    std::string pixels(
        static_cast<std::size_t>(
            imageSize),
        '\0');

    HDC screenDc =
        GetDC(nullptr);

    if (screenDc == nullptr) {
        return false;
    }

    const int lines =
        GetDIBits(
            screenDc,
            bitmap,
            0,
            static_cast<UINT>(
                height),
            pixels.data(),
            &info,
            DIB_RGB_COLORS);

    ReleaseDC(
        nullptr,
        screenDc);

    if (lines == 0) {
        return false;
    }

    HANDLE file =
        CreateFileW(
            path,
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (file ==
        INVALID_HANDLE_VALUE) {
        return false;
    }

    BITMAPFILEHEADER fileHeader{};

    fileHeader.bfType =
        0x4d42;

    fileHeader.bfOffBits =
        sizeof(BITMAPFILEHEADER) +
        sizeof(BITMAPINFOHEADER);

    fileHeader.bfSize =
        fileHeader.bfOffBits +
        imageSize;

    DWORD written = 0;

    bool success =
        WriteFile(
            file,
            &fileHeader,
            sizeof(fileHeader),
            &written,
            nullptr) != FALSE;

    if (success) {
        success =
            WriteFile(
                file,
                &info.bmiHeader,
                sizeof(info.bmiHeader),
                &written,
                nullptr) != FALSE;
    }

    if (success) {
        success =
            WriteFile(
                file,
                pixels.data(),
                imageSize,
                &written,
                nullptr) != FALSE;
    }

    CloseHandle(file);

    return success;
}

std::string captureWinlogonDesktop()
{
    HDESK desktop =
        OpenDesktopW(
            L"Winlogon",
            0,
            FALSE,
            DESKTOP_READOBJECTS |
                DESKTOP_WRITEOBJECTS);

    if (desktop == nullptr) {
        return
            "OPEN_FAILED ERROR=" +
            std::to_string(
                GetLastError());
    }

    if (!SetThreadDesktop(
            desktop)) {
        const DWORD error =
            GetLastError();

        CloseDesktop(desktop);

        return
            "SET_THREAD_DESKTOP_FAILED ERROR=" +
            std::to_string(error);
    }

    const int x =
        GetSystemMetrics(
            SM_XVIRTUALSCREEN);

    const int y =
        GetSystemMetrics(
            SM_YVIRTUALSCREEN);

    const int width =
        GetSystemMetrics(
            SM_CXVIRTUALSCREEN);

    const int height =
        GetSystemMetrics(
            SM_CYVIRTUALSCREEN);

    if (
        width <= 0 ||
        height <= 0
    ) {
        CloseDesktop(desktop);

        return
            "INVALID_SCREEN_GEOMETRY";
    }

    HDC screenDc =
        GetDC(nullptr);

    if (screenDc == nullptr) {
        const DWORD error =
            GetLastError();

        CloseDesktop(desktop);

        return
            "GET_DC_FAILED ERROR=" +
            std::to_string(error);
    }

    HDC memoryDc =
        CreateCompatibleDC(
            screenDc);

    if (memoryDc == nullptr) {
        const DWORD error =
            GetLastError();

        ReleaseDC(
            nullptr,
            screenDc);

        CloseDesktop(desktop);

        return
            "CREATE_DC_FAILED ERROR=" +
            std::to_string(error);
    }

    HBITMAP bitmap =
        CreateCompatibleBitmap(
            screenDc,
            width,
            height);

    if (bitmap == nullptr) {
        const DWORD error =
            GetLastError();

        DeleteDC(memoryDc);

        ReleaseDC(
            nullptr,
            screenDc);

        CloseDesktop(desktop);

        return
            "CREATE_BITMAP_FAILED ERROR=" +
            std::to_string(error);
    }

    HGDIOBJ oldObject =
        SelectObject(
            memoryDc,
            bitmap);

    const BOOL copied =
        BitBlt(
            memoryDc,
            0,
            0,
            width,
            height,
            screenDc,
            x,
            y,
            SRCCOPY |
                CAPTUREBLT);

    SelectObject(
        memoryDc,
        oldObject);

    DeleteDC(memoryDc);

    ReleaseDC(
        nullptr,
        screenDc);

    if (!copied) {
        const DWORD error =
            GetLastError();

        DeleteObject(bitmap);
        CloseDesktop(desktop);

        return
            "BITBLT_FAILED ERROR=" +
            std::to_string(error);
    }

    const bool saved =
        writeBitmapFile(
            L"C:\\ProgramData\\ScottiBYTE-Assist-Winlogon.bmp",
            bitmap,
            width,
            height);

    DeleteObject(bitmap);
    CloseDesktop(desktop);

    if (!saved) {
        return
            "SAVE_FAILED ERROR=" +
            std::to_string(
                GetLastError());
    }

    return
        "CAPTURED " +
        std::to_string(width) +
        "x" +
        std::to_string(height);
}

DWORD WINAPI winlogonCaptureThread(
    LPVOID)
{
    /*
     * Give the tester time to raise a UAC prompt
     * after launching the helper.
     */
    Sleep(10000);

    const std::string result =
        captureWinlogonDesktop();

    HANDLE file =
        CreateFileW(
            L"C:\\ProgramData\\ScottiBYTE-Assist-Winlogon-Capture.txt",
            GENERIC_WRITE,
            FILE_SHARE_READ |
                FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (file !=
        INVALID_HANDLE_VALUE) {
        DWORD written = 0;

        WriteFile(
            file,
            result.data(),
            static_cast<DWORD>(
                result.size()),
            &written,
            nullptr);

        CloseHandle(file);
    }

    return 0;
}

void startWinlogonCaptureThread()
{
    HANDLE thread =
        CreateThread(
            nullptr,
            0,
            winlogonCaptureThread,
            nullptr,
            0,
            nullptr);

    if (thread != nullptr) {
        CloseHandle(thread);
    }
}

DWORD WINAPI winlogonThreadProbe(
    LPVOID parameter)
{
    auto *result =
        static_cast<std::string *>(
            parameter);

    HDESK desktop =
        OpenDesktopW(
            L"Winlogon",
            0,
            FALSE,
            DESKTOP_READOBJECTS |
                DESKTOP_WRITEOBJECTS |
                DESKTOP_SWITCHDESKTOP);

    if (desktop == nullptr) {
        *result =
            "OPEN_FAILED ERROR=" +
            std::to_string(
                GetLastError());

        return 1;
    }

    if (!SetThreadDesktop(
            desktop)) {
        *result =
            "SET_THREAD_DESKTOP_FAILED ERROR=" +
            std::to_string(
                GetLastError());

        CloseDesktop(desktop);

        return 2;
    }

    HDESK threadDesktop =
        GetThreadDesktop(
            GetCurrentThreadId());

    DWORD requiredBytes = 0;

    GetUserObjectInformationW(
        threadDesktop,
        UOI_NAME,
        nullptr,
        0,
        &requiredBytes);

    if (requiredBytes == 0) {
        *result =
            "ATTACHED NAME=UNKNOWN";

        CloseDesktop(desktop);

        return 0;
    }

    std::wstring name(
        requiredBytes /
            sizeof(wchar_t),
        L'\0');

    if (!GetUserObjectInformationW(
            threadDesktop,
            UOI_NAME,
            name.data(),
            requiredBytes,
            &requiredBytes)) {
        *result =
            "ATTACHED NAME_ERROR=" +
            std::to_string(
                GetLastError());

        CloseDesktop(desktop);

        return 0;
    }

    while (
        !name.empty() &&
        name.back() == L'\0'
    ) {
        name.pop_back();
    }

    *result =
        "ATTACHED NAME=" +
        wideToUtf8(name);

    CloseDesktop(desktop);

    return 0;
}

std::string winlogonThreadStatus()
{
    std::string result =
        "NOT_RUN";

    HANDLE thread =
        CreateThread(
            nullptr,
            0,
            winlogonThreadProbe,
            &result,
            0,
            nullptr);

    if (thread == nullptr) {
        return
            "CREATE_THREAD_FAILED ERROR=" +
            std::to_string(
                GetLastError());
    }

    WaitForSingleObject(
        thread,
        INFINITE);

    CloseHandle(thread);

    return result;
}

std::string currentInputDesktop()
{
    HDESK desktop =
        OpenInputDesktop(
            0,
            FALSE,
            DESKTOP_READOBJECTS);

    if (desktop == nullptr) {
        return
            "UNAVAILABLE ERROR=" +
            std::to_string(
                GetLastError());
    }

    DWORD requiredBytes = 0;

    GetUserObjectInformationW(
        desktop,
        UOI_NAME,
        nullptr,
        0,
        &requiredBytes);

    if (requiredBytes == 0) {
        CloseDesktop(desktop);

        return
            "OPEN NAME=UNKNOWN";
    }

    std::wstring name(
        requiredBytes /
            sizeof(wchar_t),
        L'\0');

    if (!GetUserObjectInformationW(
            desktop,
            UOI_NAME,
            name.data(),
            requiredBytes,
            &requiredBytes)) {
        const DWORD error =
            GetLastError();

        CloseDesktop(desktop);

        return
            "OPEN NAME_ERROR=" +
            std::to_string(error);
    }

    CloseDesktop(desktop);

    while (
        !name.empty() &&
        name.back() == L'\0'
    ) {
        name.pop_back();
    }

    return
        "OPEN NAME=" +
        wideToUtf8(name);
}

std::string currentUser()
{
    wchar_t userName[256]{};

    DWORD length =
        static_cast<DWORD>(
            std::size(userName));

    if (!GetUserNameW(
            userName,
            &length)) {
        return
            "UNKNOWN ERROR=" +
            std::to_string(
                GetLastError());
    }

    return wideToUtf8(
        std::wstring(userName));
}

}

int main()
{
    DWORD sessionId =
        0xffffffff;

    ProcessIdToSessionId(
        GetCurrentProcessId(),
        &sessionId);

    std::ofstream output(
        "C:\\ProgramData\\ScottiBYTE-Assist-UAC-Helper.txt",
        std::ios::trunc);

    if (!output) {
        return 1;
    }

    output
        << "PROCESS_SESSION="
        << sessionId
        << '\n';

    output
        << "USER="
        << currentUser()
        << '\n';

    output
        << "WINLOGON_DESKTOP="
        << winlogonDesktopStatus()
        << '\n';

    output
        << "WINLOGON_THREAD="
        << winlogonThreadStatus()
        << '\n';

    output.flush();

    startWinlogonCaptureThread();

    std::string previousDesktop;

    constexpr int sampleCount =
        120;

    for (
        int sample = 0;
        sample < sampleCount;
        ++sample
    ) {
        const std::string desktop =
            currentInputDesktop();

        if (desktop != previousDesktop) {
            output
                << "TICK_MS="
                << sample * 250
                << " INPUT_DESKTOP="
                << desktop
                << '\n';

            output.flush();

            previousDesktop =
                desktop;
        }

        Sleep(250);
    }

    output
        << "WATCH_COMPLETE\n";

    output.flush();

    return 0;
}
