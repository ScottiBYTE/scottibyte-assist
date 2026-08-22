#include "windows_terminal_session.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

int main(
    int argc,
    char *argv[])
{
    QCoreApplication app(
        argc,
        argv);

    WindowsTerminalSession session;

    QObject::connect(
        &session,
        &WindowsTerminalSession::dataReady,
        &app,
        [](
            const QByteArray &data)
        {
            fwrite(
                data.constData(),
                1,
                static_cast<size_t>(
                    data.size()),
                stdout);

            fflush(stdout);
        });

    QObject::connect(
        &session,
        &WindowsTerminalSession::errorOccurred,
        &app,
        [](
            const QString &message)
        {
            fprintf(
                stderr,
                "\nERROR: %s\n",
                message.toUtf8().constData());

            fflush(stderr);
        });

    QObject::connect(
        &session,
        &WindowsTerminalSession::exited,
        &app,
        [&app](
            int exitCode)
        {
            fprintf(
                stdout,
                "\nEXIT: %d\n",
                exitCode);

            fflush(stdout);

            app.quit();
        });

    if (!session.start(
            100,
            30)) {
        return 1;
    }

    QTimer::singleShot(
        800,
        &app,
        [&session]()
        {
            session.writeData(
                QByteArray(
                    "Write-Output 'CONPTY-POWERSHELL-OK'\r"));
        });

    QTimer::singleShot(
        1300,
        &app,
        [&session]()
        {
            session.writeData(
                QByteArray(
                    "hostname\r"));
        });

    QTimer::singleShot(
        1800,
        &app,
        [&session]()
        {
            session.writeData(
                QByteArray(
                    "whoami\r"));
        });

    QTimer::singleShot(
        2300,
        &app,
        [&session]()
        {
            session.writeData(
                QByteArray(
                    "Get-Location\r"));
        });

    QTimer::singleShot(
        2800,
        &app,
        [&session]()
        {
            session.resize(
                120,
                40);

            session.writeData(
                QByteArray(
                    "Write-Output 'CONPTY-RESIZE-OK'\r"));
        });

    QTimer::singleShot(
        3500,
        &app,
        [&session]()
        {
            session.writeData(
                QByteArray(
                    "exit\r"));
        });

    QTimer::singleShot(
        10000,
        &app,
        [&app]()
        {
            fprintf(
                stderr,
                "\nERROR: ConPTY test timed out.\n");

            app.exit(2);
        });

    return app.exec();
}
