#include "windows_terminal_session.h"

#include <QDebug>
#include <QString>

#include <vector>

namespace
{

QString windowsErrorMessage(
    DWORD error)
{
    wchar_t *buffer = nullptr;

    const DWORD length =
        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            reinterpret_cast<wchar_t *>(
                &buffer),
            0,
            nullptr);

    QString result;

    if (
        length > 0 &&
        buffer != nullptr
    ) {
        result =
            QString::fromWCharArray(
                buffer,
                static_cast<int>(length))
                .trimmed();
    } else {
        result =
            QStringLiteral(
                "Windows error %1")
                .arg(error);
    }

    if (buffer != nullptr) {
        LocalFree(buffer);
    }

    return result;
}

}

WindowsTerminalSession::WindowsTerminalSession(
    QObject *parent)
    : QObject(parent)
{
    outputTimer_.setInterval(10);
    processTimer_.setInterval(50);

    connect(
        &outputTimer_,
        &QTimer::timeout,
        this,
        &WindowsTerminalSession::pollOutput);

    connect(
        &processTimer_,
        &QTimer::timeout,
        this,
        &WindowsTerminalSession::pollProcess);
}

WindowsTerminalSession::~WindowsTerminalSession()
{
    close();
}

bool WindowsTerminalSession::resolveConPtyApi()
{
    kernel32_ =
        GetModuleHandleW(
            L"kernel32.dll");

    if (kernel32_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not open kernel32.dll."));
        return false;
    }

    createPseudoConsole_ =
        reinterpret_cast<CreatePseudoConsoleFn>(
            GetProcAddress(
                kernel32_,
                "CreatePseudoConsole"));

    resizePseudoConsole_ =
        reinterpret_cast<ResizePseudoConsoleFn>(
            GetProcAddress(
                kernel32_,
                "ResizePseudoConsole"));

    closePseudoConsole_ =
        reinterpret_cast<ClosePseudoConsoleFn>(
            GetProcAddress(
                kernel32_,
                "ClosePseudoConsole"));

    if (
        createPseudoConsole_ == nullptr ||
        resizePseudoConsole_ == nullptr ||
        closePseudoConsole_ == nullptr
    ) {
        emit errorOccurred(
            QStringLiteral(
                "Windows ConPTY APIs are unavailable."));
        return false;
    }

    return true;
}

bool WindowsTerminalSession::start(
    int columns,
    int rows)
{
    close();

    if (!resolveConPtyApi()) {
        return false;
    }

    SECURITY_ATTRIBUTES security = {};
    security.nLength =
        sizeof(security);

    security.bInheritHandle =
        TRUE;

    if (!CreatePipe(
            &inputRead_,
            &inputWrite_,
            &security,
            0)) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
        cleanup();
        return false;
    }

    if (!CreatePipe(
            &outputRead_,
            &outputWrite_,
            &security,
            0)) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
        cleanup();
        return false;
    }

    SetHandleInformation(
        inputWrite_,
        HANDLE_FLAG_INHERIT,
        0);

    SetHandleInformation(
        outputRead_,
        HANDLE_FLAG_INHERIT,
        0);

    COORD size = {
        static_cast<SHORT>(
            qBound(
                1,
                columns,
                32767)),
        static_cast<SHORT>(
            qBound(
                1,
                rows,
                32767))
    };

    const HRESULT createResult =
        createPseudoConsole_(
            size,
            inputRead_,
            outputWrite_,
            0,
            &pseudoConsole_);

    if (FAILED(createResult)) {
        emit errorOccurred(
            QStringLiteral(
                "CreatePseudoConsole failed: 0x%1")
                .arg(
                    static_cast<qulonglong>(
                        createResult),
                    8,
                    16,
                    QLatin1Char('0')));
        cleanup();
        return false;
    }

    SIZE_T attributeBytes = 0;

    InitializeProcThreadAttributeList(
        nullptr,
        1,
        0,
        &attributeBytes);

    attributeList_ =
        reinterpret_cast<
            LPPROC_THREAD_ATTRIBUTE_LIST>(
                HeapAlloc(
                    GetProcessHeap(),
                    0,
                    attributeBytes));

    if (attributeList_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not allocate process attribute list."));
        cleanup();
        return false;
    }

    if (!InitializeProcThreadAttributeList(
            attributeList_,
            1,
            0,
            &attributeBytes)) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
        cleanup();
        return false;
    }

    if (!UpdateProcThreadAttribute(
            attributeList_,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            pseudoConsole_,
            sizeof(pseudoConsole_),
            nullptr,
            nullptr)) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
        cleanup();
        return false;
    }

    STARTUPINFOEXW startup = {};

    startup.StartupInfo.cb =
        sizeof(startup);

    startup.lpAttributeList =
        attributeList_;

    PROCESS_INFORMATION processInfo = {};

    std::wstring command =
        L"powershell.exe -NoLogo";

    std::vector<wchar_t> commandBuffer(
        command.begin(),
        command.end());

    commandBuffer.push_back(
        L'\0');

    const BOOL created =
        CreateProcessW(
            nullptr,
            commandBuffer.data(),
            nullptr,
            nullptr,
            FALSE,
            EXTENDED_STARTUPINFO_PRESENT |
                CREATE_UNICODE_ENVIRONMENT,
            nullptr,
            nullptr,
            &startup.StartupInfo,
            &processInfo);

    if (!created) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
        cleanup();
        return false;
    }

    processHandle_ =
        processInfo.hProcess;

    threadHandle_ =
        processInfo.hThread;

    CloseHandle(inputRead_);
    inputRead_ = nullptr;

    CloseHandle(outputWrite_);
    outputWrite_ = nullptr;

    outputTimer_.start();
    processTimer_.start();

    return true;
}

void WindowsTerminalSession::writeData(
    const QByteArray &data)
{
    if (
        inputWrite_ == nullptr ||
        data.isEmpty()
    ) {
        return;
    }

    DWORD bytesWritten = 0;

    if (!WriteFile(
            inputWrite_,
            data.constData(),
            static_cast<DWORD>(
                data.size()),
            &bytesWritten,
            nullptr)) {
        emit errorOccurred(
            windowsErrorMessage(
                GetLastError()));
    }
}

void WindowsTerminalSession::resize(
    int columns,
    int rows)
{
    if (
        pseudoConsole_ == nullptr ||
        resizePseudoConsole_ == nullptr
    ) {
        return;
    }

    COORD size = {
        static_cast<SHORT>(
            qBound(
                1,
                columns,
                32767)),
        static_cast<SHORT>(
            qBound(
                1,
                rows,
                32767))
    };

    const HRESULT result =
        resizePseudoConsole_(
            pseudoConsole_,
            size);

    if (FAILED(result)) {
        emit errorOccurred(
            QStringLiteral(
                "ResizePseudoConsole failed: 0x%1")
                .arg(
                    static_cast<qulonglong>(
                        result),
                    8,
                    16,
                    QLatin1Char('0')));
    }
}

void WindowsTerminalSession::close()
{
    outputTimer_.stop();
    processTimer_.stop();

    if (inputWrite_ != nullptr) {
        CloseHandle(
            inputWrite_);

        inputWrite_ = nullptr;
    }

    if (
        processHandle_ != nullptr &&
        WaitForSingleObject(
            processHandle_,
            0) == WAIT_TIMEOUT
    ) {
        TerminateProcess(
            processHandle_,
            0);
    }

    cleanup();
}

bool WindowsTerminalSession::isRunning() const
{
    return
        processHandle_ != nullptr &&
        WaitForSingleObject(
            processHandle_,
            0) == WAIT_TIMEOUT;
}

void WindowsTerminalSession::pollOutput()
{
    if (outputRead_ == nullptr) {
        return;
    }

    DWORD available = 0;

    if (!PeekNamedPipe(
            outputRead_,
            nullptr,
            0,
            nullptr,
            &available,
            nullptr)) {
        return;
    }

    if (available == 0) {
        return;
    }

    QByteArray buffer;

    buffer.resize(
        static_cast<int>(
            qMin<DWORD>(
                available,
                65536)));

    DWORD bytesRead = 0;

    if (!ReadFile(
            outputRead_,
            buffer.data(),
            static_cast<DWORD>(
                buffer.size()),
            &bytesRead,
            nullptr)) {
        return;
    }

    if (bytesRead == 0) {
        return;
    }

    buffer.resize(
        static_cast<int>(
            bytesRead));

    emit dataReady(buffer);
}

void WindowsTerminalSession::pollProcess()
{
    if (processHandle_ == nullptr) {
        return;
    }

    if (
        WaitForSingleObject(
            processHandle_,
            0) != WAIT_OBJECT_0
    ) {
        return;
    }

    DWORD exitCode = 0;

    GetExitCodeProcess(
        processHandle_,
        &exitCode);

    outputTimer_.stop();
    processTimer_.stop();

    emit exited(
        static_cast<int>(
            exitCode));

    cleanup();
}

void WindowsTerminalSession::cleanup()
{
    if (
        attributeList_ != nullptr
    ) {
        DeleteProcThreadAttributeList(
            attributeList_);

        HeapFree(
            GetProcessHeap(),
            0,
            attributeList_);

        attributeList_ = nullptr;
    }

    if (
        pseudoConsole_ != nullptr &&
        closePseudoConsole_ != nullptr
    ) {
        closePseudoConsole_(
            pseudoConsole_);

        pseudoConsole_ = nullptr;
    }

    if (inputRead_ != nullptr) {
        CloseHandle(inputRead_);
        inputRead_ = nullptr;
    }

    if (inputWrite_ != nullptr) {
        CloseHandle(inputWrite_);
        inputWrite_ = nullptr;
    }

    if (outputRead_ != nullptr) {
        CloseHandle(outputRead_);
        outputRead_ = nullptr;
    }

    if (outputWrite_ != nullptr) {
        CloseHandle(outputWrite_);
        outputWrite_ = nullptr;
    }

    if (threadHandle_ != nullptr) {
        CloseHandle(threadHandle_);
        threadHandle_ = nullptr;
    }

    if (processHandle_ != nullptr) {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
}
