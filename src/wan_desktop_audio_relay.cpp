#include "wan_desktop_audio_relay.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QWebSocket>

WanDesktopAudioRelay::WanDesktopAudioRelay(
    QObject *parent)
    : QObject(parent)
{
}

WanDesktopAudioRelay::~WanDesktopAudioRelay()
{
    disconnectFromServer();
}

void WanDesktopAudioRelay::connectForSession(
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
                "Desktop audio relay configuration is invalid."));
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
                    "Desktop audio relay WebSocket connected."));

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
        &WanDesktopAudioRelay::
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

            emit audioPacketReceived(
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
                    "Desktop audio relay WebSocket disconnected."));
        });

    socket_->open(
        webSocketUrl_);
}

void WanDesktopAudioRelay::disconnectFromServer()
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

void WanDesktopAudioRelay::sendAudioPacket(
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

void WanDesktopAudioRelay::sendSubscription()
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

void WanDesktopAudioRelay::processTextMessage(
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
            QStringLiteral("desktop-audio"));

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
                "Dedicated desktop audio relay ready."));
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

void WanDesktopAudioRelay::sendJson(
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

void WanDesktopAudioRelay::setReady(
    bool ready)
{
    if (ready_ == ready) {
        return;
    }

    ready_ = ready;

    emit readyChanged(
        ready_);
}
