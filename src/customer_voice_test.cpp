#include "customer_voice_audio.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

namespace
{

void printUsage()
{
    QTextStream(stderr)
        << "Usage:\n"
        << "  customer-voice-test receiver <port> [output-node]\n"
        << "  customer-voice-test sender <host> <port> [input-node]\n"
        << "  customer-voice-test duplex "
           "<host> <send-port> <listen-port> "
           "[input-node] [output-node]\n";
}

}

int main(
    int argc,
    char *argv[])
{
    QCoreApplication application(
        argc,
        argv);

    const QStringList arguments =
        application.arguments();

    if (arguments.size() < 3) {
        printUsage();
        return 1;
    }

    CustomerVoiceAudio audio;

    QObject::connect(
        &audio,
        &CustomerVoiceAudio::statusChanged,
        &application,
        [](
            const QString &message)
        {
            QTextStream(stdout)
                << message
                << Qt::endl;
        });

    QObject::connect(
        &audio,
        &CustomerVoiceAudio::errorOccurred,
        &application,
        [
            &application
        ](
            const QString &message)
        {
            QTextStream(stderr)
                << "Error: "
                << message
                << Qt::endl;

            application.exit(2);
        });

    const QString mode =
        arguments.at(1).toLower();

    bool started = false;

    if (mode == QStringLiteral("receiver")) {
        bool validPort = false;

        const quint16 port =
            arguments.at(2).toUShort(
                &validPort);

        if (!validPort || port == 0) {
            printUsage();
            return 1;
        }

        const QString outputNode =
            arguments.size() >= 4
                ? arguments.at(3)
                : QString();

        started =
            audio.startProviderReceiver(
                port,
                outputNode);
    } else if (
        mode == QStringLiteral("sender")) {
        if (arguments.size() < 4) {
            printUsage();
            return 1;
        }

        bool validPort = false;

        const quint16 port =
            arguments.at(3).toUShort(
                &validPort);

        if (!validPort || port == 0) {
            printUsage();
            return 1;
        }

        const QString inputNode =
            arguments.size() >= 5
                ? arguments.at(4)
                : QString();

        started =
            audio.startCustomerSender(
                arguments.at(2),
                port,
                inputNode);
    } else if (
        mode == QStringLiteral("duplex")) {
        if (arguments.size() < 5) {
            printUsage();
            return 1;
        }

        bool validSendPort = false;
        bool validListenPort = false;

        const quint16 sendPort =
            arguments.at(3).toUShort(
                &validSendPort);

        const quint16 listenPort =
            arguments.at(4).toUShort(
                &validListenPort);

        if (
            !validSendPort ||
            sendPort == 0 ||
            !validListenPort ||
            listenPort == 0) {
            printUsage();
            return 1;
        }

        const QString inputNode =
            arguments.size() >= 6
                ? arguments.at(5)
                : QString();

        const QString outputNode =
            arguments.size() >= 7
                ? arguments.at(6)
                : QString();

        const bool receiverStarted =
            audio.startProviderReceiver(
                listenPort,
                outputNode);

        const bool senderStarted =
            audio.startCustomerSender(
                arguments.at(2),
                sendPort,
                inputNode);

        started =
            receiverStarted &&
            senderStarted;

        if (!started) {
            audio.stop();
        }
    } else {
        printUsage();
        return 1;
    }

    if (!started) {
        return 2;
    }

    QTextStream(stdout)
        << "Press Ctrl+C to stop."
        << Qt::endl;

    return application.exec();
}
