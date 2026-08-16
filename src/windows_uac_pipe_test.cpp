#include <windows.h>

#include <iostream>
#include <string>

namespace
{

constexpr wchar_t PipeName[] =
    L"\\\\.\\pipe\\ScottiBYTEAssistPrivileged";

}

int main(
    int argc,
    char **argv)
{
    const std::string request =
        argc >= 2
            ? argv[1]
            : "PING";

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

    DWORD bytesWritten = 0;

    if (!WriteFile(
            pipe,
            request.data(),
            static_cast<DWORD>(
                request.size()),
            &bytesWritten,
            nullptr)) {
        std::cerr
            << "Could not write request. "
            << "Windows error "
            << GetLastError()
            << '\n';

        CloseHandle(pipe);
        return 1;
    }

    char response[1024]{};
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

    std::cout
        << "Service response: "
        << std::string(
               response,
               response + bytesRead)
        << '\n';

    return 0;
}
