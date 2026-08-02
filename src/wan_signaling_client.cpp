#include "wan_signaling_client.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
              this))
{
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
        &QWebSocket::disconnected,
        this,
        [this]()
        {
            const bool wasActive =
                state_ != State::Idle;

            state_ = State::Idle;

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
            fail(
                socket_->errorString());
        });
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
            emit relayReady();
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

        offset += chunkSize;
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

void WanSignalingClient::fail(
    const QString &message)
{
    emit errorOccurred(
        message.isEmpty()
            ? QStringLiteral(
                  "The Assist signaling "
                  "operation failed.")
            : message);
}
