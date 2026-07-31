#include "pipewire_echo_cancel.h"

#include <QCoreApplication>
#include <QTextStream>

int main(
    int argc,
    char *argv[])
{
    QCoreApplication application(
        argc,
        argv);

    const QStringList arguments =
        application.arguments();

    if (arguments.size() != 3) {
        QTextStream(stderr)
            << "Usage:\n"
            << "  pipewire-echo-cancel-test "
               "<input-node> <output-node>\n";

        return 1;
    }

    PipeWireEchoCancel echoCancel;

    if (!echoCancel.start(
            arguments.at(1),
            arguments.at(2))) {
        QTextStream(stderr)
            << "Error: "
            << echoCancel.errorString()
            << Qt::endl;

        return 2;
    }

    QTextStream(stdout)
        << "Echo cancellation loaded.\n"
        << "Source: "
        << echoCancel.sourceName()
        << "\n"
        << "Sink: "
        << echoCancel.sinkName()
        << "\n"
        << "Press Enter to unload."
        << Qt::endl;

    QTextStream input(stdin);
    input.readLine();

    echoCancel.stop();

    QTextStream(stdout)
        << "Echo cancellation unloaded."
        << Qt::endl;

    return 0;
}
