#include <windows.h>

#include <iostream>
#include <string>

namespace
{

constexpr wchar_t PipeName[] =
    L"\\\\.\\pipe\\ScottiBYTEAssistPrivileged";

}

int main()
{
    HANDLE pipe =
        CreateFileW(
            PipeName,
            GENERIC_READ |
                GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        std::cerr
            << "Could not open privileged pipe. "
            << "Windows error "
            << GetLastError()
            << '\n';

        return 1;
    }

    constexpr char request[] =
        "PING";

    DWORD bytesWritten = 0;

    if (!WriteFile(
            pipe,
            request,
            sizeof(request) - 1,
            &bytesWritten,
            nullptr)) {
        std::cerr
            << "Could not write PING. "
            << "Windows error "
            << GetLastError()
            << '\n';

        CloseHandle(pipe);
        return 1;
    }

    char response[64]{};
    DWORD bytesRead = 0;

    if (!ReadFile(
            pipe,
            response,
            sizeof(response) - 1,
            &bytesRead,
            nullptr)) {
        std::cerr
            << "Could not read response. "
            << "Windows error "
            << GetLastError()
            << '\n';

        CloseHandle(pipe);
        return 1;
    }

    CloseHandle(pipe);

    const std::string reply(
        response,
        response + bytesRead);

    std::cout
        << "Service response: "
        << reply
        << '\n';

    return reply == "PONG"
        ? 0
        : 1;
}
