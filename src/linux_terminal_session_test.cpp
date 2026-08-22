#include "linux_terminal_session.h"

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

    LinuxTerminalSession terminal;

    QObject::connect(
        &terminal,
        &LinuxTerminalSession::dataReady,
        &app,
        [](
            const QByteArray &data)
        {
            std::fwrite(
                data.constData(),
                1,
                static_cast<size_t>(
                    data.size()),
                stdout);

            std::fflush(stdout);
        });

    QObject::connect(
        &terminal,
        &LinuxTerminalSession::errorOccurred,
        &app,
        [](
            const QString &message)
        {
            std::fprintf(
                stderr,
                "ERROR: %s\n",
                message.toUtf8().constData());
        });

    QObject::connect(
        &terminal,
        &LinuxTerminalSession::exited,
        &app,
        [&app](
            int exitCode)
        {
            std::fprintf(
                stderr,
                "\nEXIT: %d\n",
                exitCode);

            app.quit();
        });

    if (!terminal.start(
            100,
            30)) {
        return 1;
    }

    QTimer::singleShot(
        500,
        &app,
        [&terminal]()
        {
            terminal.writeData(
                QByteArray(
                    "printf 'REMOTE-PTY-OK\\n'\n"));
        });

    QTimer::singleShot(
        1000,
        &app,
        [&terminal]()
        {
            terminal.writeData(
                QByteArray(
                    "printf 'TERM=%s SHELL=%s\\n' \"$TERM\" \"$SHELL\"\n"));
        });

    QTimer::singleShot(
        1500,
        &app,
        [&terminal]()
        {
            terminal.writeData(
                QByteArray(
                    "stty size\n"));
        });

    QTimer::singleShot(
        2000,
        &app,
        [&terminal]()
        {
            terminal.resize(
                120,
                40);

            terminal.writeData(
                QByteArray(
                    "stty size\n"));
        });

    QTimer::singleShot(
        2500,
        &app,
        [&terminal]()
        {
            terminal.writeData(
                QByteArray(
                    "exit\n"));
        });

    return app.exec();
}
