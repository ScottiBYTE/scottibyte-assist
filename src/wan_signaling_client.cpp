#include "wan_signaling_client.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QWebSocket>

namespace
{

QString normalizedCode(
    QString code)
{
    code.remove(' ');
    return code.trimmed();
}

QUrl apiUrl(
    const QUrl &baseUrl,
    const QString &path)
{
    QUrl result = baseUrl;

    QString basePath =
        result.path();

    if (basePath.endsWith('/')) {
        basePath.chop(1);
    }

    result.setPath(
        basePath + path);

    return result;
}

QString responseMessage(
    const QByteArray &body,
    const QString &fallback)
{
    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            body,
            &parseError);

    if (
        parseError.error ==
            QJsonParseError::NoError &&
        document.isObject()
    ) {
        const QString message =
            document.object()
                .value(
                    QStringLiteral("message"))
                .toString();

        if (!message.isEmpty()) {
            return message;
        }
    }

    return fallback;
}

}

WanSignalingClient::WanSignalingClient(
    QObject *parent)
    : QObject(parent),
      network_(
          new QNetworkAccessManager(this)),
      socket_(
          new QWebSocket(
              QString(),
              QWebSocketProtocol::VersionLatest,
              this)),
      relayHeartbeatTimer_(
          new QTimer(this)),
      relayPongDeadlineTimer_(
          new QTimer(this))
{
    relayHeartbeatTimer_->setInterval(
        3000);

    relayPongDeadlineTimer_->setInterval(
        20000);

    relayPongDeadlineTimer_->setSingleShot(
        true);

    connect(
        socket_,
        &QWebSocket::textMessageReceived,
        this,
        &WanSignalingClient::processTextMessage);

    connect(
        socket_,
        &QWebSocket::binaryMessageReceived,
        this,
        [this](
            const QByteArray &message)
        {
            if (
                state_ != State::Subscribed ||
                !relayReady_ ||
                message.isEmpty()
            ) {
                return;
            }

            emit relayBytesReceived(
                message);
        });

    connect(
        socket_,
        &QWebSocket::pong,
        this,
        [this](
            quint64,
            const QByteArray &)
        {
            if (!relayReady_) {
                return;
            }

            lastPongMs_ =
                QDateTime::currentMSecsSinceEpoch();

            relayPongDeadlineTimer_->stop();
        });

    connect(
        relayHeartbeatTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (
                state_ != State::Subscribed ||
                !relayReady_ ||
                socket_->state() !=
                    QAbstractSocket::ConnectedState
            ) {
                stopRelayHeartbeat();
                return;
            }

            if (
                relayPongDeadlineTimer_->
                    isActive()
            ) {
                return;
            }

            /*
             * Do not queue a heartbeat behind a
             * substantial desktop/voice backlog.
             * The deadline begins when ping() is
             * called, not when the frame actually
             * leaves the socket.
             */
            if (
                socket_->bytesToWrite() >
                    64 * 1024
            ) {
                return;
            }

            lastPingMs_ =
                QDateTime::currentMSecsSinceEpoch();

            socket_->ping(
                QByteArrayLiteral(
                    "assist-relay-heartbeat"));

            relayPongDeadlineTimer_->start();
        });

    connect(
        relayPongDeadlineTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (
                state_ != State::Subscribed ||
                !relayReady_ ||
                socket_->state() !=
                    QAbstractSocket::ConnectedState
            ) {
                return;
            }

            /*
             * A pong can be delayed while a large
             * desktop frame is moving through the
             * shared WebSocket queue. Do not abort
             * an otherwise connected relay here.
             *
             * The Assist server performs its own
             * native WebSocket heartbeat and will
             * terminate a genuinely dead client.
             * The next client heartbeat interval
             * will attempt another diagnostic ping.
             */
        });

    connect(
        socket_,
        &QWebSocket::bytesWritten,
        this,
        [this](
            qint64)
        {
            emit relayBytesQueuedChanged(
                socket_->bytesToWrite());
        });

    connect(
        socket_,
        &QWebSocket::disconnected,
        this,
        [this]()
        {
            const bool wasActive =
                state_ != State::Idle;

            if (wasActive) {
                QString reason =
                    lastError_.trimmed();

                if (reason.isEmpty()) {
                    reason =
                        socket_->
                            errorString().
                            trimmed();
                }

                if (
                    reason.isEmpty() ||
                    reason ==
                        QStringLiteral(
                            "Unknown error")
                ) {
                    reason =
                        QStringLiteral(
                            "The WebSocket connection "
                            "closed unexpectedly.");
                }

                emit disconnecting(
                    reason);
            }

            state_ = State::Idle;
            relayReady_ = false;

            stopRelayHeartbeat();

            if (wasActive) {
                emit statusChanged(
                    QStringLiteral(
                        "Disconnected from the Assist "
                        "signaling service."));

                emit disconnected();
            }
        });

    connect(
        socket_,
        qOverload<QAbstractSocket::SocketError>(
            &QWebSocket::error),
        this,
        [this](
            QAbstractSocket::SocketError)
        {
            if (state_ == State::Idle) {
                fail(
                    socket_->errorString());
                return;
            }

            handleUnexpectedDisconnect(
                socket_->errorString());
        });
}


void WanSignalingClient::startVoiceRelay()
{
    if (
        state_ != State::Subscribed ||
        !relayReady_ ||
        webSocketUrl_.isEmpty()
    ) {
        return;
    }

    if (voiceSocket_ != nullptr) {
        voiceSocket_->abort();
        voiceSocket_->deleteLater();
        voiceSocket_ = nullptr;
    }

    voiceRelayReady_ = false;

    voiceSocket_ =
        new QWebSocket(
            QString(),
            QWebSocketProtocol::VersionLatest,
            this);

    connect(
        voiceSocket_,
        &QWebSocket::connected,
        this,
        [this]()
        {
            if (role_ == Role::Supporter) {
                QJsonObject fields;

                fields.insert(
                    QStringLiteral("token"),
                    supporterToken_);

                sendVoiceJson(
                    QStringLiteral(
                        "auth.supporter"),
                    fields);

                return;
            }

            QJsonObject fields;

            fields.insert(
                QStringLiteral("code"),
                code_);

            fields.insert(
                QStringLiteral("role"),
                QStringLiteral("customer"));

            if (!deviceId_.isEmpty()) {
                fields.insert(
                    QStringLiteral("deviceId"),
                    deviceId_);
            }

            fields.insert(
                QStringLiteral("customerToken"),
                customerToken_);

            sendVoiceJson(
                QStringLiteral(
                    "session.subscribe"),
                fields);
        });

    connect(
        voiceSocket_,
        &QWebSocket::textMessageReceived,
        this,
        &WanSignalingClient::
            processVoiceTextMessage);

    connect(
        voiceSocket_,
        &QWebSocket::binaryMessageReceived,
        this,
        [this](
            const QByteArray &packet)
        {
            if (
                !voiceRelayReady_ ||
                packet.isEmpty()
            ) {
                return;
            }

            emit voiceRelayPacketReceived(
                packet);
        });

    connect(
        voiceSocket_,
        &QWebSocket::disconnected,
        this,
        [this]()
        {
            voiceRelayReady_ = false;
        });

    voiceSocket_->open(
        webSocketUrl_);
}

void WanSignalingClient::processVoiceTextMessage(
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
        QJsonObject fields;

        fields.insert(
            QStringLiteral("code"),
            code_);

        fields.insert(
            QStringLiteral("role"),
            QStringLiteral("supporter"));

        if (!deviceId_.isEmpty()) {
            fields.insert(
                QStringLiteral("deviceId"),
                deviceId_);
        }

        sendVoiceJson(
            QStringLiteral(
                "session.subscribe"),
            fields);

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

        sendVoiceJson(
            QStringLiteral(
                "session.relay.start"),
            fields);

        return;
    }

    if (
        type ==
            QStringLiteral(
                "session.relay.accepted") ||
        type ==
            QStringLiteral(
                "session.relay.ready")
    ) {
        if (
            type ==
                QStringLiteral(
                    "session.relay.ready") ||
            object.value(
                QStringLiteral("ready"))
                .toBool()
        ) {
            voiceRelayReady_ = true;
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.peer_disconnected")
    ) {
        voiceRelayReady_ = false;
    }
}

void WanSignalingClient::sendVoiceJson(
    const QString &type,
    const QJsonObject &additionalFields)
{
    if (
        voiceSocket_ == nullptr ||
        voiceSocket_->state() !=
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

    voiceSocket_->sendTextMessage(
        QString::fromUtf8(
            QJsonDocument(object)
                .toJson(
                    QJsonDocument::Compact)));
}

void WanSignalingClient::sendVoiceRelayPacket(
    const QByteArray &packet)
{
    if (
        !voiceRelayReady_ ||
        voiceSocket_ == nullptr ||
        voiceSocket_->state() !=
            QAbstractSocket::ConnectedState ||
        packet.isEmpty() ||
        packet.size() > 64 * 1024
    ) {
        return;
    }

    voiceSocket_->sendBinaryMessage(
        packet);
}

WanSignalingClient::~WanSignalingClient()
{
    disconnectFromServer();
}

void WanSignalingClient::createCustomerSession(
    const QUrl &apiBaseUrl,
    const QUrl &webSocketUrl,
    const QString &deviceId)
{
    disconnectFromServer();

    if (
        !apiBaseUrl.isValid() ||
        !webSocketUrl.isValid()
    ) {
        fail(
            QStringLiteral(
                "The Assist server URL is invalid."));
        return;
    }

    role_ = Role::Customer;
    state_ = State::CreatingSession;

    serverClientId_ = -1;
    lastPingMs_ = 0;
    lastPongMs_ = 0;
    lastError_.clear();

    apiBaseUrl_ = apiBaseUrl;
    webSocketUrl_ = webSocketUrl;
    deviceId_ =
        deviceId.trimmed().left(128);

    QNetworkRequest request(
        apiUrl(
            apiBaseUrl_,
            QStringLiteral(
                "/api/sessions")));

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral(
            "application/json"));

    QJsonObject body;

    if (!deviceId_.isEmpty()) {
        body.insert(
            QStringLiteral(
                "customerDeviceId"),
            deviceId_);
    }

    emit statusChanged(
        QStringLiteral(
            "Creating a secure Assist session..."));

    QNetworkReply *reply =
        network_->post(
            request,
            QJsonDocument(body)
                .toJson(
                    QJsonDocument::Compact));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply]()
        {
            handleCustomerSessionReply(
                reply);

            reply->deleteLater();
        });
}

void WanSignalingClient::claimSupportSession(
    const QUrl &apiBaseUrl,
    const QUrl &webSocketUrl,
    const QString &code,
    const QString &supporterToken,
    const QString &deviceId)
{
    disconnectFromServer();

    code_ =
        normalizedCode(code);

    supporterToken_ =
        supporterToken.trimmed();

    if (code_.size() != 6) {
        fail(
            QStringLiteral(
                "Enter all six digits."));
        return;
    }

    if (supporterToken_.isEmpty()) {
        fail(
            QStringLiteral(
                "A supporter credential is required."));
        return;
    }

    if (
        !apiBaseUrl.isValid() ||
        !webSocketUrl.isValid()
    ) {
        fail(
            QStringLiteral(
                "The Assist server URL is invalid."));
        return;
    }

    role_ = Role::Supporter;
    state_ = State::ClaimingSession;

    serverClientId_ = -1;
    lastPingMs_ = 0;
    lastPongMs_ = 0;
    lastError_.clear();

    apiBaseUrl_ = apiBaseUrl;
    webSocketUrl_ = webSocketUrl;
    deviceId_ =
        deviceId.trimmed().left(128);

    QNetworkRequest request(
        apiUrl(
            apiBaseUrl_,
            QStringLiteral(
                "/api/sessions/%1/claim")
                .arg(code_)));

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral(
            "application/json"));

    request.setRawHeader(
        "Authorization",
        QByteArray("Bearer ") +
            supporterToken_.toUtf8());

    QJsonObject body;

    if (!deviceId_.isEmpty()) {
        body.insert(
            QStringLiteral(
                "supporterDeviceId"),
            deviceId_);
    }

    emit statusChanged(
        QStringLiteral(
            "Claiming the Assist support session..."));

    QNetworkReply *reply =
        network_->post(
            request,
            QJsonDocument(body)
                .toJson(
                    QJsonDocument::Compact));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply]()
        {
            handleSupporterClaimReply(
                reply);

            reply->deleteLater();
        });
}

void WanSignalingClient::handleCustomerSessionReply(
    QNetworkReply *reply)
{
    const QByteArray body =
        reply->readAll();

    if (
        reply->error() !=
            QNetworkReply::NoError
    ) {
        fail(
            responseMessage(
                body,
                reply->errorString()));
        return;
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            body,
            &parseError);

    if (
        parseError.error !=
            QJsonParseError::NoError ||
        !document.isObject()
    ) {
        fail(
            QStringLiteral(
                "The Assist server returned "
                "an invalid session response."));
        return;
    }

    const QJsonObject object =
        document.object();

    const QJsonObject session =
        object.value(
            QStringLiteral("session"))
            .toObject();

    code_ =
        normalizedCode(
            session.value(
                QStringLiteral("code"))
                .toString());

    customerToken_ =
        object.value(
            QStringLiteral(
                "customerToken"))
            .toString();

    if (
        code_.size() != 6 ||
        customerToken_.isEmpty()
    ) {
        fail(
            QStringLiteral(
                "The Assist server did not return "
                "valid session credentials."));
        return;
    }

    emit sessionCodeAssigned(
        code_);

    beginWebSocketConnection();
}

void WanSignalingClient::handleSupporterClaimReply(
    QNetworkReply *reply)
{
    const QByteArray body =
        reply->readAll();

    if (
        reply->error() !=
            QNetworkReply::NoError
    ) {
        fail(
            responseMessage(
                body,
                reply->errorString()));
        return;
    }

    beginWebSocketConnection();
}

void WanSignalingClient::beginWebSocketConnection()
{
    state_ = State::OpeningWebSocket;

    emit statusChanged(
        QStringLiteral(
            "Connecting to the Assist "
            "signaling service..."));

    socket_->open(
        webSocketUrl_);
}

void WanSignalingClient::processTextMessage(
    const QString &message)
{
    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            message.toUtf8(),
            &parseError);

    if (
        parseError.error !=
            QJsonParseError::NoError ||
        !document.isObject()
    ) {
        fail(
            QStringLiteral(
                "The Assist signaling service "
                "sent invalid JSON."));
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
            "connection.ready")
    ) {
        serverClientId_ =
            object.value(
                QStringLiteral(
                    "clientId"))
                .toInt(-1);

        const int protocolVersion =
            object.value(
                QStringLiteral(
                    "protocolVersion"))
                .toInt();

        if (protocolVersion != 4) {
            fail(
                QStringLiteral(
                    "The Assist signaling protocol "
                    "version is not supported."));
            return;
        }

        if (role_ == Role::Supporter) {
            sendSupporterAuthentication();
        } else if (
            role_ == Role::Customer
        ) {
            sendSessionSubscription();
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "auth.supporter.accepted")
    ) {
        if (role_ == Role::Supporter) {
            sendSessionSubscription();
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.subscribed")
    ) {
        state_ = State::Subscribed;

        emit statusChanged(
            QStringLiteral(
                "Connected to the secure "
                "Assist session."));

        emit sessionSubscribed();
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
            relayReady_ = true;
            startRelayHeartbeat();
            emit relayReady();
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.ready")
    ) {
        if (!relayReady_) {
            relayReady_ = true;
            startRelayHeartbeat();
            emit relayReady();
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.relay.peer_disconnected")
    ) {
        if (relayReady_) {
            handleUnexpectedDisconnect(
                QStringLiteral(
                    "The Assist relay peer "
                    "disconnected."));
        }

        return;
    }

    if (
        type ==
        QStringLiteral(
            "session.candidate")
    ) {
        const QJsonObject payload =
            object.value(
                QStringLiteral("payload"))
                .toObject();

        const QString kind =
            payload.value(
                QStringLiteral("kind"))
                .toString()
                .trimmed()
                .toLower();

        if (
            kind ==
            QStringLiteral("request")
        ) {
            emit candidateRequestReceived();
            return;
        }

        if (
            kind ==
            QStringLiteral("relay-request")
        ) {
            emit relayRequestReceived();
            return;
        }

        /*
         * An absent kind remains compatible with
         * protocol-v4 candidate payloads created
         * before candidate requests were added.
         */
        if (
            !kind.isEmpty() &&
            kind != QStringLiteral("tcp")
        ) {
            fail(
                QStringLiteral(
                    "The peer sent an unsupported "
                    "connection candidate."));
            return;
        }

        const QString address =
            payload.value(
                QStringLiteral("address"))
                .toString()
                .trimmed();

        const int port =
            payload.value(
                QStringLiteral("port"))
                .toInt();

        if (
            address.isEmpty() ||
            port < 1 ||
            port > 65535
        ) {
            fail(
                QStringLiteral(
                    "The peer sent an invalid "
                    "connection candidate."));
            return;
        }

        emit peerCandidateReceived(
            address,
            static_cast<quint16>(
                port));

        return;
    }


    if (
        type ==
        QStringLiteral(
            "file.offer")
    ) {
        const QJsonObject payload =
            object.value(
                QStringLiteral("payload"))
                .toObject();

        const QString transferId =
            payload.value(
                QStringLiteral("transferId"))
                .toString()
                .trimmed();

        const QString fileName =
            payload.value(
                QStringLiteral("fileName"))
                .toString()
                .trimmed();

        const qint64 fileSize =
            payload.value(
                QStringLiteral("size"))
                .toVariant()
                .toLongLong();

        if (
            transferId.isEmpty() ||
            fileName.isEmpty() ||
            fileSize < 0
        ) {
            fail(
                QStringLiteral(
                    "The peer sent an invalid "
                    "file transfer offer."));
            return;
        }

        emit fileOfferReceived(
            transferId,
            fileName,
            fileSize);

        return;
    }

    if (
        type ==
        QStringLiteral(
            "file.accept")
    ) {
        const QJsonObject payload =
            object.value(
                QStringLiteral("payload"))
                .toObject();

        const QString transferId =
            payload.value(
                QStringLiteral("transferId"))
                .toString()
                .trimmed();

        if (transferId.isEmpty()) {
            fail(
                QStringLiteral(
                    "The peer sent an invalid "
                    "file acceptance."));
            return;
        }

        emit fileAccepted(
            transferId);

        return;
    }

    if (
        type ==
        QStringLiteral(
            "file.decline")
    ) {
        const QJsonObject payload =
            object.value(
                QStringLiteral("payload"))
                .toObject();

        const QString transferId =
            payload.value(
                QStringLiteral("transferId"))
                .toString()
                .trimmed();

        if (transferId.isEmpty()) {
            fail(
                QStringLiteral(
                    "The peer sent an invalid "
                    "file decline."));
            return;
        }

        emit fileDeclined(
            transferId);

        return;
    }

    if (
        type ==
        QStringLiteral("error")
    ) {
        const QString serverMessage =
            object.value(
                QStringLiteral("message"))
                .toString();

        fail(
            serverMessage.isEmpty()
                ? QStringLiteral(
                      "The Assist server "
                      "reported an error.")
                : serverMessage);
    }
}

void WanSignalingClient::sendSupporterAuthentication()
{
    state_ =
        State::AuthenticatingSupporter;

    QJsonObject fields;

    fields.insert(
        QStringLiteral("token"),
        supporterToken_);

    sendJson(
        QStringLiteral(
            "auth.supporter"),
        fields);
}

void WanSignalingClient::sendSessionSubscription()
{
    state_ =
        State::Subscribing;

    QJsonObject fields;

    fields.insert(
        QStringLiteral("code"),
        code_);

    fields.insert(
        QStringLiteral("role"),
        role_ == Role::Supporter
            ? QStringLiteral("supporter")
            : QStringLiteral("customer"));

    if (!deviceId_.isEmpty()) {
        fields.insert(
            QStringLiteral("deviceId"),
            deviceId_);
    }

    if (role_ == Role::Customer) {
        fields.insert(
            QStringLiteral(
                "customerToken"),
            customerToken_);
    }

    sendJson(
        QStringLiteral(
            "session.subscribe"),
        fields);
}



void WanSignalingClient::createFileTransfer(
    const QString &fileName,
    qint64 fileSize)
{
    if (
        state_ != State::Subscribed ||
        code_.isEmpty() ||
        fileName.trimmed().isEmpty() ||
        fileSize < 0
    ) {
        return;
    }

    QUrl url = apiBaseUrl_;

    QString path = url.path();

    if (path.endsWith(QChar('/'))) {
        path.chop(1);
    }

    url.setPath(
        path +
        QStringLiteral(
            "/api/sessions/") +
        code_ +
        QStringLiteral(
            "/transfers"));

    QNetworkRequest request(url);

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral(
            "application/json"));

    if (role_ == Role::Supporter) {
        request.setRawHeader(
            QByteArrayLiteral("Authorization"),
            QByteArrayLiteral("Bearer ") +
                supporterToken_.toUtf8());
    } else if (role_ == Role::Customer) {
        request.setRawHeader(
            QByteArrayLiteral("X-Customer-Token"),
            customerToken_.toUtf8());
    }

    QJsonObject body;

    body.insert(
        QStringLiteral("fileName"),
        fileName.trimmed());

    body.insert(
        QStringLiteral("size"),
        fileSize);

    QNetworkReply *reply =
        network_->post(
            request,
            QJsonDocument(body)
                .toJson(
                    QJsonDocument::Compact));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [
            this,
            reply,
            fileName,
            fileSize
        ]()
        {
            const QByteArray body =
                reply->readAll();

            if (
                reply->error() !=
                    QNetworkReply::NoError
            ) {
                fail(
                    responseMessage(
                        body,
                        reply->errorString()));

                reply->deleteLater();
                return;
            }

            QJsonParseError parseError;

            const QJsonDocument document =
                QJsonDocument::fromJson(
                    body,
                    &parseError);

            if (
                parseError.error !=
                    QJsonParseError::NoError ||
                !document.isObject()
            ) {
                fail(
                    QStringLiteral(
                        "The Assist server returned "
                        "an invalid file transfer response."));

                reply->deleteLater();
                return;
            }

            const QJsonObject object =
                document.object();

            const QString transferId =
                object.value(
                    QStringLiteral("id"))
                    .toString()
                    .trimmed();

            if (transferId.isEmpty()) {
                fail(
                    QStringLiteral(
                        "The Assist server did not return "
                        "a file transfer ID."));

                reply->deleteLater();
                return;
            }

            emit fileTransferCreated(
                transferId,
                fileName,
                fileSize);

            reply->deleteLater();
        });
}

void WanSignalingClient::sendFileOffer(
    const QString &transferId,
    const QString &fileName,
    qint64 fileSize)
{
    if (
        state_ != State::Subscribed ||
        transferId.trimmed().isEmpty() ||
        fileName.trimmed().isEmpty() ||
        fileSize < 0
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("transferId"),
        transferId.trimmed());

    payload.insert(
        QStringLiteral("fileName"),
        fileName.trimmed());

    payload.insert(
        QStringLiteral("size"),
        fileSize);

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral("file.offer"),
        fields);
}

void WanSignalingClient::sendFileAccept(
    const QString &transferId)
{
    if (
        state_ != State::Subscribed ||
        transferId.trimmed().isEmpty()
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("transferId"),
        transferId.trimmed());

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral("file.accept"),
        fields);
}

void WanSignalingClient::sendFileDecline(
    const QString &transferId)
{
    if (
        state_ != State::Subscribed ||
        transferId.trimmed().isEmpty()
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("transferId"),
        transferId.trimmed());

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral("file.decline"),
        fields);
}

void WanSignalingClient::sendCandidateRequest()
{
    if (
        state_ != State::Subscribed
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("kind"),
        QStringLiteral("request"));

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral(
            "session.candidate"),
        fields);
}

void WanSignalingClient::sendRelayRequest()
{
    if (
        state_ != State::Subscribed
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("kind"),
        QStringLiteral("relay-request"));

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral(
            "session.candidate"),
        fields);
}

void WanSignalingClient::sendCandidate(
    const QString &address,
    quint16 port)
{
    if (
        state_ != State::Subscribed ||
        address.trimmed().isEmpty() ||
        port == 0
    ) {
        return;
    }

    QJsonObject payload;

    payload.insert(
        QStringLiteral("kind"),
        QStringLiteral("tcp"));

    payload.insert(
        QStringLiteral("address"),
        address.trimmed());

    payload.insert(
        QStringLiteral("port"),
        static_cast<int>(port));

    QJsonObject fields;

    fields.insert(
        QStringLiteral("payload"),
        payload);

    sendJson(
        QStringLiteral(
            "session.candidate"),
        fields);
}

void WanSignalingClient::startRelay()
{
    if (
        state_ != State::Subscribed
    ) {
        return;
    }

    relayReady_ = false;

    sendJson(
        QStringLiteral(
            "session.relay.start"));
}

void WanSignalingClient::sendRelayBytes(
    const QByteArray &bytes)
{
    if (
        state_ != State::Subscribed ||
        !relayReady_ ||
        bytes.isEmpty() ||
        socket_->state() !=
            QAbstractSocket::ConnectedState
    ) {
        return;
    }

    int offset = 0;

    while (offset < bytes.size()) {
        const int chunkSize =
            qMin(
                relayChunkMaximum_,
                bytes.size() - offset);

        socket_->sendBinaryMessage(
            bytes.mid(
                offset,
                chunkSize));

        emit relayBytesQueuedChanged(
            socket_->bytesToWrite());

        offset += chunkSize;
    }
}

void WanSignalingClient::startRelayHeartbeat()
{
    if (
        state_ != State::Subscribed ||
        !relayReady_ ||
        socket_->state() !=
            QAbstractSocket::ConnectedState
    ) {
        return;
    }

    relayPongDeadlineTimer_->stop();

    if (!relayHeartbeatTimer_->isActive()) {
        relayHeartbeatTimer_->start();
    }
}

void WanSignalingClient::stopRelayHeartbeat()
{
    relayHeartbeatTimer_->stop();
    relayPongDeadlineTimer_->stop();
}

void WanSignalingClient::handleUnexpectedDisconnect(
    const QString &message)
{
    if (state_ == State::Idle) {
        return;
    }

    lastError_ = message;

    fail(message);

    state_ = State::Idle;
    relayReady_ = false;
    voiceRelayReady_ = false;

    if (voiceSocket_ != nullptr) {
        voiceSocket_->abort();
        voiceSocket_->deleteLater();
        voiceSocket_ = nullptr;
    }

    stopRelayHeartbeat();

    emit statusChanged(
        QStringLiteral(
            "Disconnected from the Assist "
            "signaling service."));

    emit disconnected();

    if (
        socket_->state() !=
        QAbstractSocket::UnconnectedState
    ) {
        socket_->abort();
    }
}

void WanSignalingClient::sendJson(
    const QString &type,
    const QJsonObject &additionalFields)
{
    if (
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

void WanSignalingClient::disconnectFromServer()
{
    state_ = State::Idle;
    role_ = Role::Inactive;

    code_.clear();
    customerToken_.clear();
    supporterToken_.clear();
    deviceId_.clear();
    relayReady_ = false;
    voiceRelayReady_ = false;

    if (voiceSocket_ != nullptr) {
        voiceSocket_->abort();
        voiceSocket_->deleteLater();
        voiceSocket_ = nullptr;
    }

    stopRelayHeartbeat();

    if (
        socket_->state() !=
        QAbstractSocket::UnconnectedState
    ) {
        socket_->close();
    }
}

bool WanSignalingClient::isSubscribed() const
{
    return state_ ==
        State::Subscribed;
}

QString WanSignalingClient::sessionCode() const
{
    return code_;
}

QUrl WanSignalingClient::webSocketUrl() const
{
    return webSocketUrl_;
}

QString WanSignalingClient::deviceId() const
{
    return deviceId_;
}

QString WanSignalingClient::voiceRole() const
{
    if (role_ == Role::Supporter) {
        return QStringLiteral("supporter");
    }

    if (role_ == Role::Customer) {
        return QStringLiteral("customer");
    }

    return {};
}

QString WanSignalingClient::voiceToken() const
{
    if (role_ == Role::Supporter) {
        return supporterToken_;
    }

    if (role_ == Role::Customer) {
        return customerToken_;
    }

    return {};
}

QString WanSignalingClient::diagnosticSummary(
    const QString &label) const
{
    QString roleText;

    switch (role_) {
    case Role::Inactive:
        roleText =
            QStringLiteral("Inactive");
        break;

    case Role::Customer:
        roleText =
            QStringLiteral("Customer");
        break;

    case Role::Supporter:
        roleText =
            QStringLiteral("Supporter");
        break;
    }

    QString stateText;

    switch (state_) {
    case State::Idle:
        stateText =
            QStringLiteral("Idle");
        break;

    case State::CreatingSession:
        stateText =
            QStringLiteral("Creating session");
        break;

    case State::ClaimingSession:
        stateText =
            QStringLiteral("Claiming session");
        break;

    case State::OpeningWebSocket:
        stateText =
            QStringLiteral("Opening WebSocket");
        break;

    case State::AuthenticatingSupporter:
        stateText =
            QStringLiteral("Authenticating supporter");
        break;

    case State::Subscribing:
        stateText =
            QStringLiteral("Subscribing");
        break;

    case State::Subscribed:
        stateText =
            QStringLiteral("Subscribed");
        break;
    }

    QString socketStateText;

    switch (socket_->state()) {
    case QAbstractSocket::UnconnectedState:
        socketStateText =
            QStringLiteral("Disconnected");
        break;

    case QAbstractSocket::HostLookupState:
        socketStateText =
            QStringLiteral("Resolving host");
        break;

    case QAbstractSocket::ConnectingState:
        socketStateText =
            QStringLiteral("Connecting");
        break;

    case QAbstractSocket::ConnectedState:
        socketStateText =
            QStringLiteral("Connected");
        break;

    case QAbstractSocket::BoundState:
        socketStateText =
            QStringLiteral("Bound");
        break;

    case QAbstractSocket::ClosingState:
        socketStateText =
            QStringLiteral("Closing");
        break;

    case QAbstractSocket::ListeningState:
        socketStateText =
            QStringLiteral("Listening");
        break;
    }

    const qint64 now =
        QDateTime::currentMSecsSinceEpoch();

    const auto elapsedText =
        [now](qint64 timestamp)
        {
            if (timestamp <= 0) {
                return QStringLiteral("Never");
            }

            return QStringLiteral("%1 seconds ago")
                .arg(
                    static_cast<double>(
                        now - timestamp) /
                        1000.0,
                    0,
                    'f',
                    1);
        };

    QString sessionText =
        code_.isEmpty()
            ? QStringLiteral("None")
            : code_;

    if (sessionText.size() == 6) {
        sessionText.insert(
            3,
            QChar(' '));
    }

    return QStringLiteral(
        "%1\n"
        "Role: %2\n"
        "Internal state: %3\n"
        "WebSocket state: %4\n"
        "Server client ID: %5\n"
        "Session code: %6\n"
        "Device ID: %7\n"
        "Relay ready: %8\n"
        "Queued outbound bytes: %9\n"
        "Heartbeat deadline: %10\n"
        "Last ping: %11\n"
        "Last pong: %12\n"
        "Last error: %13")
        .arg(
            label,
            roleText,
            stateText,
            socketStateText,
            serverClientId_ >= 0
                ? QString::number(
                      serverClientId_)
                : QStringLiteral("None"),
            sessionText,
            deviceId_.isEmpty()
                ? QStringLiteral("None")
                : deviceId_,
            relayReady_
                ? QStringLiteral("Yes")
                : QStringLiteral("No"),
            QString::number(
                socket_->bytesToWrite()),
            relayPongDeadlineTimer_->isActive()
                ? QStringLiteral("Active")
                : QStringLiteral("Inactive"),
            elapsedText(lastPingMs_),
            elapsedText(lastPongMs_),
            lastError_.isEmpty()
                ? QStringLiteral("None")
                : lastError_);
}

void WanSignalingClient::fail(
    const QString &message)
{
    lastError_ = message;
    emit errorOccurred(
        message.isEmpty()
            ? QStringLiteral(
                  "The Assist signaling "
                  "operation failed.")
            : message);
}
