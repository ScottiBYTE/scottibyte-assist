#include "wan_signaling_client.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QProcessEnvironment>
#include <QTimer>
#include <QUrl>

#include <iostream>

int main(
    int argc,
    char *argv[])
{
    QCoreApplication application(
        argc,
        argv);

    const QString supporterToken =
        QProcessEnvironment::
            systemEnvironment()
            .value(
                QStringLiteral(
                    "ASSIST_SUPPORTER_TOKEN"));

    if (supporterToken.isEmpty()) {
        std::cerr
            << "FAIL: ASSIST_SUPPORTER_TOKEN "
               "is not set."
            << std::endl;

        return 1;
    }

    const QUrl apiBaseUrl(
        QStringLiteral(
            "http://assist:3089"));

    const QUrl webSocketUrl(
        QStringLiteral(
            "ws://assist:3089/ws"));

    WanSignalingClient customer;
    WanSignalingClient supporter;

    QString sessionCode;

    bool customerSubscribed = false;
    bool supporterSubscribed = false;
    bool candidateSent = false;

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(20000);

    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        [&application]()
        {
            std::cerr
                << "FAIL: Timed out waiting for "
                   "the complete signaling test."
                << std::endl;

            application.exit(1);
        });

    QObject::connect(
        &customer,
        &WanSignalingClient::statusChanged,
        &application,
        [](const QString &status)
        {
            std::cout
                << "CUSTOMER: "
                << status.toStdString()
                << std::endl;
        });

    QObject::connect(
        &supporter,
        &WanSignalingClient::statusChanged,
        &application,
        [](const QString &status)
        {
            std::cout
                << "SUPPORTER: "
                << status.toStdString()
                << std::endl;
        });

    QObject::connect(
        &customer,
        &WanSignalingClient::sessionCodeAssigned,
        &application,
        [&](const QString &code)
        {
            sessionCode = code;

            std::cout
                << "CODE: "
                << code.toStdString()
                << std::endl;
        });

    QObject::connect(
        &customer,
        &WanSignalingClient::sessionSubscribed,
        &application,
        [&]()
        {
            customerSubscribed = true;

            std::cout
                << "CUSTOMER: Subscribed."
                << std::endl;

            supporter.claimSupportSession(
                apiBaseUrl,
                webSocketUrl,
                sessionCode,
                supporterToken,
                QStringLiteral(
                    "Mondo-2-Test-Supporter"));
        });

    QObject::connect(
        &supporter,
        &WanSignalingClient::sessionSubscribed,
        &application,
        [&]()
        {
            supporterSubscribed = true;

            std::cout
                << "SUPPORTER: Subscribed."
                << std::endl;

            if (
                customerSubscribed &&
                !candidateSent
            ) {
                candidateSent = true;

                customer.sendCandidate(
                    QStringLiteral(
                        "203.0.113.10"),
                    4242);

                std::cout
                    << "CUSTOMER: Sent test candidate."
                    << std::endl;
            }
        });

    QObject::connect(
        &supporter,
        &WanSignalingClient::peerCandidateReceived,
        &application,
        [&](const QString &address, quint16 port)
        {
            std::cout
                << "SUPPORTER: Received candidate "
                << address.toStdString()
                << ":"
                << port
                << std::endl;

            if (
                customerSubscribed &&
                supporterSubscribed &&
                address ==
                    QStringLiteral(
                        "203.0.113.10") &&
                port == 4242
            ) {
                std::cout
                    << "PASS: Customer creation, "
                       "supporter claim, both WebSocket "
                       "subscriptions, and candidate "
                       "forwarding succeeded."
                    << std::endl;

                application.exit(0);
                return;
            }

            std::cerr
                << "FAIL: Received an unexpected "
                   "candidate."
                << std::endl;

            application.exit(1);
        });

    auto connectFailure =
        [&application](
            const QString &side,
            const QString &message)
        {
            std::cerr
                << "FAIL ["
                << side.toStdString()
                << "]: "
                << message.toStdString()
                << std::endl;

            application.exit(1);
        };

    QObject::connect(
        &customer,
        &WanSignalingClient::errorOccurred,
        &application,
        [&](const QString &message)
        {
            connectFailure(
                QStringLiteral("customer"),
                message);
        });

    QObject::connect(
        &supporter,
        &WanSignalingClient::errorOccurred,
        &application,
        [&](const QString &message)
        {
            connectFailure(
                QStringLiteral("supporter"),
                message);
        });

    timeout.start();

    customer.createCustomerSession(
        apiBaseUrl,
        webSocketUrl,
        QHostInfo::localHostName() +
            QStringLiteral(
                "-Test-Customer"));

    return application.exec();
}
