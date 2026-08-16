#include <windows.h>
#include <sddl.h>

#include <fstream>
#include <string>
#include <sstream>

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

std::string userObjectName(
    HANDLE object)
{
    DWORD requiredBytes = 0;

    GetUserObjectInformationW(
        object,
        UOI_NAME,
        nullptr,
        0,
        &requiredBytes);

    if (requiredBytes == 0) {
        return
            "UNKNOWN ERROR=" +
            std::to_string(
                GetLastError());
    }

    std::wstring name(
        requiredBytes /
            sizeof(wchar_t),
        L'\0');

    if (!GetUserObjectInformationW(
            object,
            UOI_NAME,
            name.data(),
            requiredBytes,
            &requiredBytes)) {
        return
            "UNKNOWN ERROR=" +
            std::to_string(
                GetLastError());
    }

    while (
        !name.empty() &&
        name.back() == L'\0'
    ) {
        name.pop_back();
    }

    return wideToUtf8(name);
}

std::string processWindowStationName()
{
    HWINSTA station =
        GetProcessWindowStation();

    if (station == nullptr) {
        return
            "UNAVAILABLE ERROR=" +
            std::to_string(
                GetLastError());
    }

    return userObjectName(
        station);
}

std::string currentThreadDesktopName()
{
    HDESK desktop =
        GetThreadDesktop(
            GetCurrentThreadId());

    if (desktop == nullptr) {
        return
            "UNAVAILABLE ERROR=" +
            std::to_string(
                GetLastError());
    }

    return userObjectName(
        desktop);
}

std::string sidToString(
    PSID sid)
{
    if (
        sid == nullptr ||
        !IsValidSid(sid)
    ) {
        return "INVALID";
    }

    LPWSTR value =
        nullptr;

    if (!ConvertSidToStringSidW(
            sid,
            &value)) {
        return
            "ERROR=" +
            std::to_string(
                GetLastError());
    }

    const std::string result =
        wideToUtf8(
            std::wstring(value));

    LocalFree(value);

    return result;
}

std::string tokenDiagnostics()
{
    HANDLE token =
        nullptr;

    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token)) {
        return
            "TOKEN_OPEN_ERROR=" +
            std::to_string(
                GetLastError());
    }

    std::string result;

    DWORD sessionId = 0;
    DWORD size = 0;

    if (GetTokenInformation(
            token,
            TokenSessionId,
            &sessionId,
            sizeof(sessionId),
            &size)) {
        result +=
            "TOKEN_SESSION=" +
            std::to_string(
                sessionId);
    } else {
        result +=
            "TOKEN_SESSION_ERROR=" +
            std::to_string(
                GetLastError());
    }

    DWORD uiAccess = 0;
    size = 0;

    if (GetTokenInformation(
            token,
            TokenUIAccess,
            &uiAccess,
            sizeof(uiAccess),
            &size)) {
        result +=
            " TOKEN_UIACCESS=" +
            std::to_string(
                uiAccess);
    } else {
        result +=
            " TOKEN_UIACCESS_ERROR=" +
            std::to_string(
                GetLastError());
    }

    DWORD userBytes = 0;

    GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0,
        &userBytes);

    if (
        userBytes > 0 &&
        GetLastError() ==
            ERROR_INSUFFICIENT_BUFFER
    ) {
        std::string buffer(
            static_cast<std::size_t>(
                userBytes),
            '\0');

        auto *tokenUser =
            reinterpret_cast<TOKEN_USER *>(
                buffer.data());

        if (GetTokenInformation(
                token,
                TokenUser,
                tokenUser,
                userBytes,
                &userBytes)) {
            result +=
                " TOKEN_USER=" +
                sidToString(
                    tokenUser->
                        User.Sid);
        }
    }

    DWORD integrityBytes = 0;

    GetTokenInformation(
        token,
        TokenIntegrityLevel,
        nullptr,
        0,
        &integrityBytes);

    if (
        integrityBytes > 0 &&
        GetLastError() ==
            ERROR_INSUFFICIENT_BUFFER
    ) {
        std::string buffer(
            static_cast<std::size_t>(
                integrityBytes),
            '\0');

        auto *label =
            reinterpret_cast<
                TOKEN_MANDATORY_LABEL *>(
                    buffer.data());

        if (GetTokenInformation(
                token,
                TokenIntegrityLevel,
                label,
                integrityBytes,
                &integrityBytes)) {
            result +=
                " TOKEN_INTEGRITY_SID=" +
                sidToString(
                    label->
                        Label.Sid);
        }
    }

    DWORD groupsBytes = 0;

    GetTokenInformation(
        token,
        TokenGroups,
        nullptr,
        0,
        &groupsBytes);

    bool logonSidFound =
        false;

    if (
        groupsBytes > 0 &&
        GetLastError() ==
            ERROR_INSUFFICIENT_BUFFER
    ) {
        std::string buffer(
            static_cast<std::size_t>(
                groupsBytes),
            '\0');

        auto *groups =
            reinterpret_cast<
                TOKEN_GROUPS *>(
                    buffer.data());

        if (GetTokenInformation(
                token,
                TokenGroups,
                groups,
                groupsBytes,
                &groupsBytes)) {
            for (
                DWORD index = 0;
                index <
                    groups->GroupCount;
                ++index
            ) {
                const SID_AND_ATTRIBUTES &group =
                    groups->Groups[index];

                if (
                    (
                        group.Attributes &
                        SE_GROUP_LOGON_ID
                    ) ==
                    SE_GROUP_LOGON_ID
                ) {
                    result +=
                        " TOKEN_LOGON_SID=" +
                        sidToString(
                            group.Sid);

                    logonSidFound =
                        true;

                    break;
                }
            }
        }
    }

    if (!logonSidFound) {
        result +=
            " TOKEN_LOGON_SID=MISSING";
    }

    CloseHandle(token);

    return result;
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

bool currentUserIsSystem()
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token)) {
        return false;
    }

    DWORD bytes = 0;

    GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0,
        &bytes);

    if (
        bytes == 0 ||
        GetLastError() !=
            ERROR_INSUFFICIENT_BUFFER
    ) {
        CloseHandle(token);
        return false;
    }

    std::string buffer(
        static_cast<std::size_t>(bytes),
        '\0');

    auto *user =
        reinterpret_cast<TOKEN_USER *>(
            buffer.data());

    if (!GetTokenInformation(
            token,
            TokenUser,
            user,
            bytes,
            &bytes)) {
        CloseHandle(token);
        return false;
    }

    BYTE systemSidBuffer[
        SECURITY_MAX_SID_SIZE]{};

    DWORD systemSidSize =
        sizeof(systemSidBuffer);

    const BOOL created =
        CreateWellKnownSid(
            WinLocalSystemSid,
            nullptr,
            systemSidBuffer,
            &systemSidSize);

    const bool isSystem =
        created &&
        EqualSid(
            user->User.Sid,
            systemSidBuffer);

    CloseHandle(token);

    return isSystem;
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

bool sendBrokerVirtualKey(
    WORD virtualKey,
    bool pressed)
{
    if (virtualKey == 0) {
        return false;
    }

    INPUT input{};
    input.type =
        INPUT_KEYBOARD;

    input.ki.wVk =
        virtualKey;

    if (isExtendedVirtualKey(
            virtualKey)) {
        input.ki.dwFlags |=
            KEYEVENTF_EXTENDEDKEY;
    }

    if (!pressed) {
        input.ki.dwFlags |=
            KEYEVENTF_KEYUP;
    }

    return
        SendInput(
            1,
            &input,
            sizeof(INPUT)) == 1;
}

bool sendBrokerMouseButton(
    DWORD flags)
{
    INPUT input{};
    input.type =
        INPUT_MOUSE;

    input.mi.dwFlags =
        flags;

    return
        SendInput(
            1,
            &input,
            sizeof(INPUT)) == 1;
}

std::string processBrokerCommand(
    const std::string &request,
    bool &quit)
{
    std::istringstream input(
        request);

    std::string command;
    input >> command;

    if (command == "PING") {
        return "PONG";
    }

    if (command == "QUIT") {
        quit = true;
        return "OK";
    }

    if (command == "MOVE") {
        int x = 0;
        int y = 0;

        if (!(input >> x >> y)) {
            return "ERROR BAD_MOVE";
        }

        if (!SetCursorPos(
                x,
                y)) {
            return
                "ERROR MOVE " +
                std::to_string(
                    GetLastError());
        }

        return "OK";
    }

    if (
        command == "LDOWNAT" ||
        command == "LUPAT"
    ) {
        int x = 0;
        int y = 0;

        if (!(input >> x >> y)) {
            return "ERROR BAD_LEFT_AT";
        }

        if (!SetCursorPos(
                x,
                y)) {
            return
                "ERROR MOVE " +
                std::to_string(
                    GetLastError());
        }

        const DWORD flags =
            command == "LDOWNAT"
                ? MOUSEEVENTF_LEFTDOWN
                : MOUSEEVENTF_LEFTUP;

        if (!sendBrokerMouseButton(
                flags)) {
            return
                "ERROR LEFT_AT " +
                std::to_string(
                    GetLastError());
        }

        return "OK";
    }

    if (command == "LDOWN") {
        return
            sendBrokerMouseButton(
                MOUSEEVENTF_LEFTDOWN)
                ? "OK"
                : "ERROR LDOWN";
    }

    if (command == "LUP") {
        return
            sendBrokerMouseButton(
                MOUSEEVENTF_LEFTUP)
                ? "OK"
                : "ERROR LUP";
    }

    if (command == "RDOWN") {
        return
            sendBrokerMouseButton(
                MOUSEEVENTF_RIGHTDOWN)
                ? "OK"
                : "ERROR RDOWN";
    }

    if (command == "RUP") {
        return
            sendBrokerMouseButton(
                MOUSEEVENTF_RIGHTUP)
                ? "OK"
                : "ERROR RUP";
    }

    if (
        command == "KEYDOWN" ||
        command == "KEYUP"
    ) {
        unsigned int virtualKey = 0;

        if (!(input >> virtualKey) ||
            virtualKey > 0xffff) {
            return "ERROR BAD_KEY";
        }

        const bool pressed =
            command == "KEYDOWN";

        if (!sendBrokerVirtualKey(
                static_cast<WORD>(
                    virtualKey),
                pressed)) {
            return
                "ERROR KEY " +
                std::to_string(
                    GetLastError());
        }

        return "OK";
    }

    return "ERROR UNKNOWN_COMMAND";
}

bool createElevatedInputPipeSecurity(
    SECURITY_ATTRIBUTES &attributes,
    PSECURITY_DESCRIPTOR &descriptor)
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token)) {
        return false;
    }

    DWORD bytes = 0;

    GetTokenInformation(
        token,
        TokenUser,
        nullptr,
        0,
        &bytes);

    if (
        bytes == 0 ||
        GetLastError() !=
            ERROR_INSUFFICIENT_BUFFER
    ) {
        CloseHandle(token);
        return false;
    }

    std::string buffer(
        static_cast<std::size_t>(bytes),
        '\0');

    auto *user =
        reinterpret_cast<TOKEN_USER *>(
            buffer.data());

    if (!GetTokenInformation(
            token,
            TokenUser,
            user,
            bytes,
            &bytes)) {
        CloseHandle(token);
        return false;
    }

    LPWSTR userSidString = nullptr;

    if (!ConvertSidToStringSidW(
            user->User.Sid,
            &userSidString)) {
        CloseHandle(token);
        return false;
    }

    CloseHandle(token);

    const std::wstring sddl =
        std::wstring(
            L"D:"
            L"(A;;GA;;;SY)"
            L"(A;;GA;;;BA)"
            L"(A;;GRGW;;;") +
        userSidString +
        L")";

    LocalFree(
        userSidString);

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(),
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        return false;
    }

    attributes.nLength =
        sizeof(SECURITY_ATTRIBUTES);

    attributes.lpSecurityDescriptor =
        descriptor;

    attributes.bInheritHandle =
        FALSE;

    return true;
}

int runElevatedInputBroker()
{
    constexpr wchar_t pipeName[] =
        L"\\\\.\\pipe\\ScottiBYTEAssistElevatedInput";

    SECURITY_ATTRIBUTES securityAttributes{};
    PSECURITY_DESCRIPTOR securityDescriptor =
        nullptr;

    if (!createElevatedInputPipeSecurity(
            securityAttributes,
            securityDescriptor)) {
        return 20;
    }

    bool quit = false;

    while (!quit) {
        HANDLE pipe =
            CreateNamedPipeW(
                pipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE |
                    PIPE_READMODE_MESSAGE |
                    PIPE_WAIT |
                    PIPE_REJECT_REMOTE_CLIENTS,
                1,
                4096,
                4096,
                0,
                &securityAttributes);

        if (pipe ==
            INVALID_HANDLE_VALUE) {
            LocalFree(
                securityDescriptor);

            return 21;
        }

        const BOOL connected =
            ConnectNamedPipe(
                pipe,
                nullptr)
            ? TRUE
            : (
                GetLastError() ==
                ERROR_PIPE_CONNECTED
            );

        if (connected) {
            char requestBuffer[512]{};
            DWORD bytesRead = 0;

            if (ReadFile(
                    pipe,
                    requestBuffer,
                    sizeof(requestBuffer) - 1,
                    &bytesRead,
                    nullptr)) {
                const std::string request(
                    requestBuffer,
                    requestBuffer +
                        bytesRead);

                const std::string response =
                    processBrokerCommand(
                        request,
                        quit);

                DWORD bytesWritten = 0;

                WriteFile(
                    pipe,
                    response.data(),
                    static_cast<DWORD>(
                        response.size()),
                    &bytesWritten,
                    nullptr);

                FlushFileBuffers(pipe);
            }

            DisconnectNamedPipe(
                pipe);
        }

        CloseHandle(pipe);
    }

    LocalFree(
        securityDescriptor);

    return 0;
}

}

int main()
{
    if (!currentUserIsSystem()) {
        return runElevatedInputBroker();
    }

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
        << "WINDOW_STATION="
        << processWindowStationName()
        << '\n';

    output
        << "THREAD_DESKTOP="
        << currentThreadDesktopName()
        << '\n';

    output
        << "TOKEN="
        << tokenDiagnostics()
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
