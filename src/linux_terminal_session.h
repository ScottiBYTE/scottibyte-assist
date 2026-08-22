#pragma once

#include <QObject>
#include <QByteArray>
#include <QSocketNotifier>

class LinuxTerminalSession : public QObject
{
    Q_OBJECT

public:
    explicit LinuxTerminalSession(
        QObject *parent = nullptr);

    ~LinuxTerminalSession() override;

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
    void readAvailable();

private:
    void cleanup();
    void checkChildExit();

    int masterFd_ = -1;
    int childPid_ = -1;

    QSocketNotifier *readNotifier_ =
        nullptr;
};
