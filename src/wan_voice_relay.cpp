#include "wan_voice_relay.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QWebSocket>

WanVoiceRelay::WanVoiceRelay(
    QObject *parent)
    : QObject(parent)
{
}

WanVoiceRelay::~WanVoiceRelay()
{
    disconnectFromServer();
}

void WanVoiceRelay::connectForSession(
    const QUrl &webSocketUrl,
    const QString &code,
    const QString &role,
    const QString &token,
    const QString &deviceId)
{
    disconnectFromServer();

    webSocketUrl_ = webSocketUrl;
    code_ = code.trimmed();
    role_ = role.trimmed().toLower();
    token_ = token;
    deviceId_ = deviceId.trimmed();

    if (
        !webSocketUrl_.isValid() ||
        code_.isEmpty() ||
        (
            role_ != QStringLiteral("supporter") &&
            role_ != QStringLiteral("customer")
        ) ||
        token_.isEmpty()
    ) {
        emit statusChanged(
            QStringLiteral(
                "Voice relay configuration is invalid."));
        return;
    }

    socket_ =
        new QWebSocket(
            QString(),
            QWebSocketProtocol::VersionLatest,
            this);

    connect(
        socket_,
        &QWebSocket::connected,
        this,
        [this]()
        {
            emit statusChanged(
                QStringLiteral(
                    "Voice relay WebSocket connected."));

            if (
                role_ ==
                QStringLiteral("supporter")
            ) {
                QJsonObject fields;

                fields.insert(
                    QStringLiteral("token"),
                    token_);

                sendJson(
                    QStringLiteral(
                        "auth.supporter"),
                    fields);
            } else {
                sendSubscription();
            }
        });

    connect(
        socket_,
        &QWebSocket::textMessageReceived,
        this,
        &WanVoiceRelay::
            processTextMessage);

    connect(
        socket_,
        &QWebSocket::binaryMessageReceived,
        this,
        [this](
            const QByteArray &packet)
        {
            if (
                !ready_ ||
                packet.isEmpty() ||
                packet.size() > 64 * 1024
            ) {
                return;
            }

            emit voicePacketReceived(
                packet);
        });

    connect(
        socket_,
        &QWebSocket::disconnected,
        this,
        [this]()
        {
            setReady(false);

            emit statusChanged(
                QStringLiteral(
                    "Voice relay WebSocket disconnected."));
        });

    socket_->open(
        webSocketUrl_);
}

void WanVoiceRelay::disconnectFromServer()
{
    setReady(false);

    if (socket_ != nullptr) {
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }

    webSocketUrl_.clear();
    code_.clear();
    role_.clear();
    token_.clear();
    deviceId_.clear();
}

void WanVoiceRelay::sendVoicePacket(
    const QByteArray &packet)
{
    if (
        !ready_ ||
        socket_ == nullptr ||
        socket_->state() !=
            QAbstractSocket::ConnectedState ||
        packet.isEmpty() ||
        packet.size() > 64 * 1024
    ) {
        return;
    }

    socket_->sendBinaryMessage(
        packet);
}

void WanVoiceRelay::sendSubscription()
{
    QJsonObject fields;

    fields.insert(
        QStringLiteral("code"),
        code_);

    fields.insert(
        QStringLiteral("role"),
        role_);

    if (!deviceId_.isEmpty()) {
        fields.insert(
            QStringLiteral("deviceId"),
            deviceId_);
    }

    if (
        role_ ==
        QStringLiteral("customer")
    ) {
        fields.insert(
            QStringLiteral(
                "customerToken"),
            token_);
    }

    sendJson(
        QStringLiteral(
            "session.subscribe"),
        fields);
}

void WanVoiceRelay::processTextMessage(
    const QString &message)
{
    QJsonParseError error;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            message.toUtf8(),
            &error);

    if (
        error.error !=
            QJsonParseError::NoError ||
        !document.isObject()
    ) {
        return;
    }

    const QJsonObject object =
        document.object();

    const QString type =
        object.value(
            QStringLiteral("type"))
            .toString();

    if (
        type ==
        QStringLiteral(
            "auth.supporter.accepted")
    ) {
        sendSubscription();
        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.subscribed")
    ) {
        QJsonObject fields;

        fields.insert(
            QStringLiteral("channel"),
            QStringLiteral("voice"));

        sendJson(
            QStringLiteral(
                "session.relay.start"),
            fields);

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.ready")
    ) {
        setReady(true);

        emit statusChanged(
            QStringLiteral(
                "Dedicated voice relay ready."));
        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.accepted")
    ) {
        if (
            object.value(
                QStringLiteral("ready"))
                .toBool()
        ) {
            setReady(true);
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.peer_disconnected")
    ) {
        setReady(false);
    }
}

void WanVoiceRelay::sendJson(
    const QString &type,
    const QJsonObject &additionalFields)
{
    if (
        socket_ == nullptr ||
        socket_->state() !=
            QAbstractSocket::ConnectedState
    ) {
        return;
    }

    QJsonObject object =
        additionalFields;

    object.insert(
        QStringLiteral("type"),
        type);

    object.insert(
        QStringLiteral("requestId"),
        QString::number(
            nextRequestId_++));

    socket_->sendTextMessage(
        QString::fromUtf8(
            QJsonDocument(object)
                .toJson(
                    QJsonDocument::Compact)));
}

void WanVoiceRelay::setReady(
    bool ready)
{
    if (ready_ == ready) {
        return;
    }

    ready_ = ready;

    emit readyChanged(
        ready_);
}
