#include "linux_terminal_session.h"
#include "remote_terminal_widget.h"

#include <QApplication>
#include <QMessageBox>
#include <QTimer>

int main(
    int argc,
    char *argv[])
{
    QApplication app(
        argc,
        argv);

    RemoteTerminalWidget terminal;

    terminal.resize(
        1000,
        650);

    terminal.setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Interactive Terminal Test"));

    LinuxTerminalSession session;

    QObject::connect(
        &session,
        &LinuxTerminalSession::dataReady,
        &terminal,
        [&terminal](
            const QByteArray &data)
        {
            terminal.feedData(
                data);
        });

    QObject::connect(
        &terminal,
        &RemoteTerminalWidget::terminalInputReady,
        &session,
        [&session](
            const QByteArray &data)
        {
            session.writeData(
                data);
        });

    QObject::connect(
        &terminal,
        &RemoteTerminalWidget::terminalResizeRequested,
        &session,
        [&session](
            int columns,
            int rows)
        {
            if (session.isRunning()) {
                session.resize(
                    columns,
                    rows);
            }
        });

    QObject::connect(
        &session,
        &LinuxTerminalSession::errorOccurred,
        &terminal,
        [&terminal](
            const QString &message)
        {
            QMessageBox::critical(
                &terminal,
                QStringLiteral(
                    "Terminal Error"),
                message);
        });

    QObject::connect(
        &session,
        &LinuxTerminalSession::exited,
        &app,
        [&terminal, &app](
            int)
        {
            terminal.close();
            app.quit();
        });

    terminal.show();
    terminal.raise();
    terminal.activateWindow();
    terminal.setFocus(
        Qt::OtherFocusReason);

    QTimer::singleShot(
        0,
        &app,
        [&terminal, &session]()
        {
            const QSize cells =
                terminal.terminalSizeCells();

            if (!session.start(
                    cells.width(),
                    cells.height())) {
                return;
            }

            terminal.setFocus(
                Qt::OtherFocusReason);
        });

    const int result =
        app.exec();

    session.close();

    return result;
}
