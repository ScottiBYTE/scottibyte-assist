#pragma once

#include <QObject>
#include <QByteArray>
#include <QTimer>

#include <windows.h>

class WindowsTerminalSession final : public QObject
{
    Q_OBJECT

public:
    explicit WindowsTerminalSession(
        QObject *parent = nullptr);

    ~WindowsTerminalSession() override;

    bool start(
        int columns,
        int rows);

    void writeData(
        const QByteArray &data);

    void resize(
        int columns,
        int rows);

    void close();

    bool isRunning() const;

signals:
    void dataReady(
        const QByteArray &data);

    void exited(
        int exitCode);

    void errorOccurred(
        const QString &message);

private slots:
    void pollOutput();
    void pollProcess();

private:
    using HPCON_LOCAL = HANDLE;

    using CreatePseudoConsoleFn =
        HRESULT (WINAPI *)(
            COORD,
            HANDLE,
            HANDLE,
            DWORD,
            HPCON_LOCAL *);

    using ResizePseudoConsoleFn =
        HRESULT (WINAPI *)(
            HPCON_LOCAL,
            COORD);

    using ClosePseudoConsoleFn =
        void (WINAPI *)(
            HPCON_LOCAL);

    bool resolveConPtyApi();

    void cleanup();

    HMODULE kernel32_ = nullptr;

    CreatePseudoConsoleFn
        createPseudoConsole_ = nullptr;

    ResizePseudoConsoleFn
        resizePseudoConsole_ = nullptr;

    ClosePseudoConsoleFn
        closePseudoConsole_ = nullptr;

    HPCON_LOCAL pseudoConsole_ = nullptr;

    HANDLE inputRead_ = nullptr;
    HANDLE inputWrite_ = nullptr;

    HANDLE outputRead_ = nullptr;
    HANDLE outputWrite_ = nullptr;

    HANDLE processHandle_ = nullptr;
    HANDLE threadHandle_ = nullptr;

    LPPROC_THREAD_ATTRIBUTE_LIST
        attributeList_ = nullptr;

    QTimer outputTimer_;
    QTimer processTimer_;
};
