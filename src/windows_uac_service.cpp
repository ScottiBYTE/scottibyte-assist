#include <windows.h>

namespace
{

constexpr wchar_t ServiceName[] =
    L"ScottiBYTEAssistService";

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

    WaitForSingleObject(
        stopEvent,
        INFINITE);

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
