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

    output.flush();

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
