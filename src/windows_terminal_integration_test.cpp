#include "remote_terminal_widget.h"
#include "windows_terminal_session.h"

#include <QApplication>
#include <QMessageBox>

int main(
    int argc,
    char *argv[])
{
    QApplication app(
        argc,
        argv);

    RemoteTerminalWidget terminal;
    WindowsTerminalSession session;

    terminal.resize(
        1000,
        650);

    terminal.setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Windows Terminal Integration Test"));

    QObject::connect(
        &session,
        &WindowsTerminalSession::dataReady,
        &terminal,
        &RemoteTerminalWidget::feedData);

    QObject::connect(
        &terminal,
        &RemoteTerminalWidget::terminalInputReady,
        &session,
        &WindowsTerminalSession::writeData);

    QObject::connect(
        &terminal,
        &RemoteTerminalWidget::terminalResizeRequested,
        &session,
        &WindowsTerminalSession::resize);

    QObject::connect(
        &session,
        &WindowsTerminalSession::exited,
        &app,
        [&terminal](
            int)
        {
            terminal.close();
        });

    QObject::connect(
        &session,
        &WindowsTerminalSession::errorOccurred,
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

    terminal.show();

    const QSize size =
        terminal.terminalSizeCells();

    if (!session.start(
            size.width(),
            size.height())) {
        return 1;
    }

    return app.exec();
}
