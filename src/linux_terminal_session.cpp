#include "linux_terminal_session.h"

#include <QFile>
#include <QString>

#include <cerrno>
#include <cstring>

#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

LinuxTerminalSession::LinuxTerminalSession(
    QObject *parent)
    : QObject(parent)
{
}

LinuxTerminalSession::~LinuxTerminalSession()
{
    close();
}

bool LinuxTerminalSession::start(
    int columns,
    int rows)
{
    if (isRunning()) {
        return true;
    }

    if (columns <= 0 || rows <= 0) {
        emit errorOccurred(
            QStringLiteral(
                "Invalid terminal size."));
        return false;
    }

    struct winsize size = {};

    size.ws_col =
        static_cast<unsigned short>(
            columns);

    size.ws_row =
        static_cast<unsigned short>(
            rows);

    const pid_t pid =
        ::forkpty(
            &masterFd_,
            nullptr,
            nullptr,
            &size);

    if (pid < 0) {
        const QString message =
            QString::fromLocal8Bit(
                std::strerror(errno));

        masterFd_ = -1;

        emit errorOccurred(
            QStringLiteral(
                "Unable to create terminal: %1")
                .arg(message));

        return false;
    }

    if (pid == 0) {
        const QByteArray shell =
            qgetenv("SHELL");

        const QByteArray executable =
            shell.isEmpty()
                ? QByteArray("/bin/bash")
                : shell;

        ::execl(
            executable.constData(),
            executable.constData(),
            "-l",
            static_cast<char *>(nullptr));

        _exit(127);
    }

    childPid_ =
        static_cast<int>(pid);

    readNotifier_ =
        new QSocketNotifier(
            masterFd_,
            QSocketNotifier::Read,
            this);

    connect(
        readNotifier_,
        &QSocketNotifier::activated,
        this,
        &LinuxTerminalSession::readAvailable);

    return true;
}

void LinuxTerminalSession::writeData(
    const QByteArray &data)
{
    if (!isRunning() ||
        data.isEmpty()) {
        return;
    }

    qsizetype offset = 0;

    while (offset < data.size()) {
        const ssize_t written =
            ::write(
                masterFd_,
                data.constData() + offset,
                static_cast<size_t>(
                    data.size() - offset));

        if (written > 0) {
            offset +=
                static_cast<qsizetype>(
                    written);

            continue;
        }

        if (written < 0 &&
            errno == EINTR) {
            continue;
        }

        break;
    }
}

void LinuxTerminalSession::resize(
    int columns,
    int rows)
{
    if (!isRunning() ||
        columns <= 0 ||
        rows <= 0) {
        return;
    }

    struct winsize size = {};

    size.ws_col =
        static_cast<unsigned short>(
            columns);

    size.ws_row =
        static_cast<unsigned short>(
            rows);

    if (::ioctl(
            masterFd_,
            TIOCSWINSZ,
            &size) == 0) {
        ::kill(
            static_cast<pid_t>(
                childPid_),
            SIGWINCH);
    }
}

void LinuxTerminalSession::close()
{
    if (childPid_ > 0) {
        ::kill(
            static_cast<pid_t>(
                childPid_),
            SIGHUP);
    }

    cleanup();
}

bool LinuxTerminalSession::isRunning() const
{
    return masterFd_ >= 0 &&
           childPid_ > 0;
}

void LinuxTerminalSession::readAvailable()
{
    if (masterFd_ < 0) {
        return;
    }

    QByteArray data;

    char buffer[8192];

    for (;;) {
        const ssize_t count =
            ::read(
                masterFd_,
                buffer,
                sizeof(buffer));

        if (count > 0) {
            data.append(
                buffer,
                static_cast<qsizetype>(
                    count));

            if (count <
                static_cast<ssize_t>(
                    sizeof(buffer))) {
                break;
            }

            continue;
        }

        if (count < 0 &&
            errno == EINTR) {
            continue;
        }

        if (count < 0 &&
            (errno == EAGAIN ||
             errno == EWOULDBLOCK)) {
            break;
        }

        break;
    }

    if (!data.isEmpty()) {
        emit dataReady(data);
    }

    checkChildExit();
}

void LinuxTerminalSession::checkChildExit()
{
    if (childPid_ <= 0) {
        return;
    }

    int status = 0;

    const pid_t result =
        ::waitpid(
            static_cast<pid_t>(
                childPid_),
            &status,
            WNOHANG);

    if (result <= 0) {
        return;
    }

    int exitCode = -1;

    if (WIFEXITED(status)) {
        exitCode =
            WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode =
            128 + WTERMSIG(status);
    }

    cleanup();

    emit exited(exitCode);
}

void LinuxTerminalSession::cleanup()
{
    if (readNotifier_ != nullptr) {
        readNotifier_->setEnabled(false);
        readNotifier_->deleteLater();
        readNotifier_ = nullptr;
    }

    if (masterFd_ >= 0) {
        ::close(masterFd_);
        masterFd_ = -1;
    }

    if (childPid_ > 0) {
        int status = 0;

        ::waitpid(
            static_cast<pid_t>(
                childPid_),
            &status,
            WNOHANG);

        childPid_ = -1;
    }
}
