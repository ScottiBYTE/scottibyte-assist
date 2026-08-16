#include <windows.h>

#include <iostream>
#include <string>

namespace
{

constexpr wchar_t PipeName[] =
    L"\\\\.\\pipe\\ScottiBYTEAssistElevatedInput";

}

int main(
    int argc,
    char **argv)
{
    if (argc < 2) {
        std::cerr
            << "Usage: windows-uac-input-test "
            << "<command> [arguments]\n";

        return 2;
    }

    std::string request =
        argv[1];

    for (
        int index = 2;
        index < argc;
        ++index
    ) {
        request += " ";
        request += argv[index];
    }

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

    if (pipe ==
        INVALID_HANDLE_VALUE) {
        std::cerr
            << "Could not open elevated input pipe. "
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
            << "Could not send command. "
            << "Windows error "
            << GetLastError()
            << '\n';

        CloseHandle(pipe);
        return 1;
    }

    char response[256]{};
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
        << std::string(
               response,
               response + bytesRead)
        << '\n';

    return 0;
}
