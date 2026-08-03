#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;

class WanSignalingClient final : public QObject
{
    Q_OBJECT

public:
    explicit WanSignalingClient(
        QObject *parent = nullptr);

    ~WanSignalingClient() override;

    void createCustomerSession(
        const QUrl &apiBaseUrl,
        const QUrl &webSocketUrl,
        const QString &deviceId);

    void claimSupportSession(
        const QUrl &apiBaseUrl,
        const QUrl &webSocketUrl,
        const QString &code,
        const QString &supporterToken,
        const QString &deviceId);

    void sendCandidateRequest();

    void sendRelayRequest();

    void sendCandidate(
        const QString &address,
        quint16 port);

    void startRelay();

    void sendRelayBytes(
        const QByteArray &bytes);

    void disconnectFromServer();

    bool isSubscribed() const;

    QString sessionCode() const;

    QString diagnosticSummary(
        const QString &label) const;

signals:
    void statusChanged(
        const QString &status);

    void sessionCodeAssigned(
        const QString &code);

    void sessionSubscribed();

    void candidateRequestReceived();

    void relayRequestReceived();

    void peerCandidateReceived(
        const QString &address,
        quint16 port);

    void relayReady();

    void relayBytesReceived(
        const QByteArray &bytes);

    void relayBytesQueuedChanged(
        qint64 bytes);

    void disconnected();

    void errorOccurred(
        const QString &message);

private:
    enum class Role
    {
        Inactive,
        Customer,
        Supporter
    };

    enum class State
    {
        Idle,
        CreatingSession,
        ClaimingSession,
        OpeningWebSocket,
        AuthenticatingSupporter,
        Subscribing,
        Subscribed
    };

    void beginWebSocketConnection();

    void handleCustomerSessionReply(
        QNetworkReply *reply);

    void handleSupporterClaimReply(
        QNetworkReply *reply);

    void processTextMessage(
        const QString &message);

    void sendSupporterAuthentication();
    void sendSessionSubscription();

    void sendJson(
        const QString &type,
        const QJsonObject &additionalFields = {});

    void startRelayHeartbeat();
    void stopRelayHeartbeat();
    void handleUnexpectedDisconnect(
        const QString &message);

    void fail(
        const QString &message);

    QNetworkAccessManager *network_ = nullptr;
    QWebSocket *socket_ = nullptr;

    QTimer *relayHeartbeatTimer_ = nullptr;
    QTimer *relayPongDeadlineTimer_ = nullptr;

    Role role_ = Role::Inactive;
    State state_ = State::Idle;

    QUrl apiBaseUrl_;
    QUrl webSocketUrl_;

    QString code_;
    QString customerToken_;
    QString supporterToken_;
    QString deviceId_;

    bool relayReady_ = false;

    int serverClientId_ = -1;

    qint64 lastPingMs_ = 0;
    qint64 lastPongMs_ = 0;

    QString lastError_;

    quint64 nextRequestId_ = 1;

    static constexpr int relayChunkMaximum_ =
        48 * 1024;
};
