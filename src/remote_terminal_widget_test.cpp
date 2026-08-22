#include "remote_terminal_widget.h"

#include <QApplication>

int main(
    int argc,
    char *argv[])
{
    QApplication app(
        argc,
        argv);

    RemoteTerminalWidget terminal;

    terminal.resize(
        900,
        500);

    terminal.setWindowTitle(
        QStringLiteral(
            "ScottiBYTE Assist — Terminal Test"));

    terminal.show();

    terminal.feedData(
        QByteArray(
            "\x1b[2J"
            "\x1b[H"
            "ScottiBYTE Assist Remote Terminal\r\n"
            "\r\n"
            "\x1b[7mReverse video test\x1b[0m\r\n"
            "\r\n"
            "$ echo hello\r\n"
            "hello\r\n"
            "$ "));

    return app.exec();
}
