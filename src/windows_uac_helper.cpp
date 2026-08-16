#include <windows.h>

#include <fstream>
#include <string>

namespace
{

std::string desktopName()
{
    HDESK desktop =
        OpenInputDesktop(
            0,
            FALSE,
            DESKTOP_READOBJECTS);

    if (desktop == nullptr) {
        return "UNAVAILABLE ERROR=" +
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
        return "OPEN NAME=UNKNOWN";
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

        return "OPEN NAME_ERROR=" +
            std::to_string(error);
    }

    CloseDesktop(desktop);

    while (
        !name.empty() &&
        name.back() == L'\0'
    ) {
        name.pop_back();
    }

    const int utf8Length =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            name.c_str(),
            static_cast<int>(
                name.size()),
            nullptr,
            0,
            nullptr,
            nullptr);

    std::string utf8Name;

    if (utf8Length > 0) {
        utf8Name.resize(
            static_cast<std::size_t>(
                utf8Length));

        WideCharToMultiByte(
            CP_UTF8,
            0,
            name.c_str(),
            static_cast<int>(
                name.size()),
            utf8Name.data(),
            utf8Length,
            nullptr,
            nullptr);
    }

    return "OPEN NAME=" + utf8Name;
}

}

int main()
{
    DWORD sessionId =
        0xffffffff;

    ProcessIdToSessionId(
        GetCurrentProcessId(),
        &sessionId);

    wchar_t userName[256]{};
    DWORD userNameLength =
        static_cast<DWORD>(
            std::size(userName));

    GetUserNameW(
        userName,
        &userNameLength);

    std::ofstream output(
        "C:\\ProgramData\\ScottiBYTE-Assist-UAC-Helper.txt",
        std::ios::trunc);

    output
        << "PROCESS_SESSION="
        << sessionId
        << '\n';

    output
        << "INPUT_DESKTOP="
        << desktopName()
        << '\n';

    const int utf8Length =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            userName,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);

    if (utf8Length > 1) {
        std::string userBuffer(
            static_cast<std::size_t>(
                utf8Length),
            '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            userName,
            -1,
            userBuffer.data(),
            utf8Length,
            nullptr,
            nullptr);

        if (
            !userBuffer.empty() &&
            userBuffer.back() == '\0'
        ) {
            userBuffer.pop_back();
        }

        output
            << "USER="
            << userBuffer
            << '\n';
    }

    return 0;
}
