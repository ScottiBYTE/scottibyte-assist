#include <windows.h>
#include <sddl.h>

#include <cstring>
#include <string>

namespace
{

constexpr wchar_t ServiceName[] =
    L"ScottiBYTEAssistService";

constexpr wchar_t PipeName[] =
    L"\\\\.\\pipe\\ScottiBYTEAssistPrivileged";

SERVICE_STATUS_HANDLE serviceStatusHandle =
    nullptr;

SERVICE_STATUS serviceStatus{};

HANDLE stopEvent =
    nullptr;

void reportServiceStatus(
    DWORD state,
    DWORD win32ExitCode = NO_ERROR,
    DWORD waitHint = 0)
{
    serviceStatus.dwServiceType =
        SERVICE_WIN32_OWN_PROCESS;

    serviceStatus.dwCurrentState =
        state;

    serviceStatus.dwWin32ExitCode =
        win32ExitCode;

    serviceStatus.dwWaitHint =
        waitHint;

    serviceStatus.dwControlsAccepted =
        state == SERVICE_START_PENDING
            ? 0
            : SERVICE_ACCEPT_STOP |
              SERVICE_ACCEPT_SHUTDOWN;

    SetServiceStatus(
        serviceStatusHandle,
        &serviceStatus);
}

DWORD WINAPI serviceControlHandler(
    DWORD control,
    DWORD,
    LPVOID,
    LPVOID)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        if (
            serviceStatus.dwCurrentState ==
            SERVICE_RUNNING
        ) {
            reportServiceStatus(
                SERVICE_STOP_PENDING,
                NO_ERROR,
                3000);

            if (stopEvent != nullptr) {
                SetEvent(stopEvent);
            }
        }

        return NO_ERROR;

    default:
        return NO_ERROR;
    }
}

bool createPipeSecurity(
    SECURITY_ATTRIBUTES &attributes,
    PSECURITY_DESCRIPTOR &descriptor)
{
    /*
     * SYSTEM: full access
     * Administrators: full access
     * Interactive users: read/write
     *
     * This is sufficient for the harmless PING/PONG
     * proof. Privileged commands will require
     * additional client authentication later.
     */
    constexpr wchar_t sddl[] =
        L"D:"
        L"(A;;GA;;;SY)"
        L"(A;;GA;;;BA)"
        L"(A;;GRGW;;;IU)";

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl,
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

void writePipeResponse(
    HANDLE pipe,
    const std::string &response)
{
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

std::string inputDesktopStatus()
{
    DWORD serviceSessionId =
        0xffffffff;

    ProcessIdToSessionId(
        GetCurrentProcessId(),
        &serviceSessionId);

    const DWORD activeConsoleSessionId =
        WTSGetActiveConsoleSessionId();

    std::string response =
        "SERVICE_SESSION=" +
        std::to_string(
            serviceSessionId) +
        " ACTIVE_CONSOLE_SESSION=" +
        std::to_string(
            activeConsoleSessionId);

    HDESK desktop =
        OpenInputDesktop(
            0,
            FALSE,
            DESKTOP_READOBJECTS);

    if (desktop == nullptr) {
        response +=
            " INPUT_DESKTOP=UNAVAILABLE"
            " ERROR=" +
            std::to_string(
                GetLastError());

        return response;
    }

    DWORD requiredBytes = 0;

    GetUserObjectInformationW(
        desktop,
        UOI_NAME,
        nullptr,
        0,
        &requiredBytes);

    if (requiredBytes == 0) {
        response +=
            " INPUT_DESKTOP=OPEN"
            " NAME=UNKNOWN";

        CloseDesktop(desktop);
        return response;
    }

    std::wstring desktopName(
        requiredBytes /
            sizeof(wchar_t),
        L'\0');

    if (!GetUserObjectInformationW(
            desktop,
            UOI_NAME,
            desktopName.data(),
            requiredBytes,
            &requiredBytes)) {
        response +=
            " INPUT_DESKTOP=OPEN"
            " NAME_ERROR=" +
            std::to_string(
                GetLastError());

        CloseDesktop(desktop);
        return response;
    }

    CloseDesktop(desktop);

    while (
        !desktopName.empty() &&
        desktopName.back() == L'\0'
    ) {
        desktopName.pop_back();
    }

    const int utf8Length =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            desktopName.c_str(),
            static_cast<int>(
                desktopName.size()),
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
            desktopName.c_str(),
            static_cast<int>(
                desktopName.size()),
            utf8Name.data(),
            utf8Length,
            nullptr,
            nullptr);
    }

    response +=
        " INPUT_DESKTOP=OPEN"
        " NAME=" +
        utf8Name;

    return response;
}

std::wstring helperExecutablePath()
{
    wchar_t modulePath[MAX_PATH]{};

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            modulePath,
            static_cast<DWORD>(
                std::size(modulePath)));

    if (
        length == 0 ||
        length >=
            static_cast<DWORD>(
                std::size(modulePath))
    ) {
        return {};
    }

    std::wstring path(
        modulePath,
        modulePath + length);

    const std::size_t slash =
        path.find_last_of(
            L"\\/");

    if (slash == std::wstring::npos) {
        return {};
    }

    path.resize(
        slash + 1);

    path +=
        L"scottibyte-assist-helper.exe";

    return path;
}

std::string launchInteractiveHelper()
{
    DWORD sessionId =
        WTSGetActiveConsoleSessionId();

    if (sessionId == 0xffffffff) {
        return
            "ERROR NO_ACTIVE_CONSOLE_SESSION";
    }

    HANDLE processToken =
        nullptr;

    if (!OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_DUPLICATE |
                TOKEN_QUERY |
                TOKEN_ASSIGN_PRIMARY,
            &processToken)) {
        return
            "ERROR OPEN_PROCESS_TOKEN " +
            std::to_string(
                GetLastError());
    }

    HANDLE primaryToken =
        nullptr;

    if (!DuplicateTokenEx(
            processToken,
            MAXIMUM_ALLOWED,
            nullptr,
            SecurityImpersonation,
            TokenPrimary,
            &primaryToken)) {
        const DWORD error =
            GetLastError();

        CloseHandle(processToken);

        return
            "ERROR DUPLICATE_TOKEN " +
            std::to_string(error);
    }

    CloseHandle(processToken);

    if (!SetTokenInformation(
            primaryToken,
            TokenSessionId,
            &sessionId,
            sizeof(sessionId))) {
        const DWORD error =
            GetLastError();

        CloseHandle(primaryToken);

        return
            "ERROR SET_SESSION " +
            std::to_string(error);
    }

    const std::wstring helperPath =
        helperExecutablePath();

    if (helperPath.empty()) {
        CloseHandle(primaryToken);

        return
            "ERROR HELPER_PATH";
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb =
        sizeof(startupInfo);

    wchar_t desktopName[] =
        L"winsta0\\default";

    startupInfo.lpDesktop =
        desktopName;

    PROCESS_INFORMATION processInformation{};

    const BOOL created =
        CreateProcessAsUserW(
            primaryToken,
            helperPath.c_str(),
            nullptr,
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInformation);

    const DWORD createError =
        created
            ? ERROR_SUCCESS
            : GetLastError();

    CloseHandle(primaryToken);

    if (!created) {
        return
            "ERROR CREATE_PROCESS " +
            std::to_string(
                createError);
    }

    CloseHandle(
        processInformation.hThread);

    CloseHandle(
        processInformation.hProcess);

    return
        "HELPER_LAUNCHED SESSION=" +
        std::to_string(
            sessionId);
}

void handlePipeClient(
    HANDLE pipe)
{
    char requestBuffer[64]{};

    DWORD bytesRead = 0;

    if (!ReadFile(
            pipe,
            requestBuffer,
            sizeof(requestBuffer) - 1,
            &bytesRead,
            nullptr)) {
        return;
    }

    const std::string request(
        requestBuffer,
        requestBuffer + bytesRead);

    if (request == "PING") {
        writePipeResponse(
            pipe,
            "PONG");

        return;
    }

    if (request == "STATUS") {
        writePipeResponse(
            pipe,
            inputDesktopStatus());

        return;
    }

    if (request == "LAUNCH_HELPER") {
        writePipeResponse(
            pipe,
            launchInteractiveHelper());

        return;
    }

    writePipeResponse(
        pipe,
        "ERROR UNKNOWN_COMMAND");
}

void runPipeServer()
{
    SECURITY_ATTRIBUTES securityAttributes{};
    PSECURITY_DESCRIPTOR securityDescriptor =
        nullptr;

    if (!createPipeSecurity(
            securityAttributes,
            securityDescriptor)) {
        return;
    }

    while (
        WaitForSingleObject(
            stopEvent,
            0) != WAIT_OBJECT_0
    ) {
        HANDLE pipe =
            CreateNamedPipeW(
                PipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE |
                    PIPE_READMODE_MESSAGE |
                    PIPE_NOWAIT,
                1,
                4096,
                4096,
                0,
                &securityAttributes);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }

        bool connected = false;

        while (
            WaitForSingleObject(
                stopEvent,
                0) != WAIT_OBJECT_0
        ) {
            if (ConnectNamedPipe(
                    pipe,
                    nullptr)) {
                connected = true;
                break;
            }

            const DWORD error =
                GetLastError();

            if (
                error ==
                ERROR_PIPE_CONNECTED
            ) {
                connected = true;
                break;
            }

            if (
                error !=
                ERROR_PIPE_LISTENING
            ) {
                break;
            }

            Sleep(50);
        }

        if (connected) {
            handlePipeClient(pipe);

            DisconnectNamedPipe(pipe);
        }

        CloseHandle(pipe);
    }

    if (securityDescriptor != nullptr) {
        LocalFree(securityDescriptor);
    }
}

void WINAPI serviceMain(
    DWORD,
    wchar_t **)
{
    serviceStatusHandle =
        RegisterServiceCtrlHandlerExW(
            ServiceName,
            serviceControlHandler,
            nullptr);

    if (serviceStatusHandle == nullptr) {
        return;
    }

    reportServiceStatus(
        SERVICE_START_PENDING,
        NO_ERROR,
        3000);

    stopEvent =
        CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);

    if (stopEvent == nullptr) {
        reportServiceStatus(
            SERVICE_STOPPED,
            GetLastError());

        return;
    }

    reportServiceStatus(
        SERVICE_RUNNING);

    runPipeServer();

    CloseHandle(stopEvent);
    stopEvent = nullptr;

    reportServiceStatus(
        SERVICE_STOPPED);
}

}

int main()
{
    SERVICE_TABLE_ENTRYW dispatchTable[] =
    {
        {
            const_cast<wchar_t *>(
                ServiceName),
            serviceMain
        },
        {
            nullptr,
            nullptr
        }
    };

    if (!StartServiceCtrlDispatcherW(
            dispatchTable)) {
        return static_cast<int>(
            GetLastError());
    }

    return 0;
}
