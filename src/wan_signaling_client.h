#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
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

    void sendCandidate(
        const QString &address,
        quint16 port);

    void disconnectFromServer();

    bool isSubscribed() const;

    QString sessionCode() const;

signals:
    void statusChanged(
        const QString &status);

    void sessionCodeAssigned(
        const QString &code);

    void sessionSubscribed();

    void candidateRequestReceived();

    void peerCandidateReceived(
        const QString &address,
        quint16 port);

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

    void fail(
        const QString &message);

    QNetworkAccessManager *network_ = nullptr;
    QWebSocket *socket_ = nullptr;

    Role role_ = Role::Inactive;
    State state_ = State::Idle;

    QUrl apiBaseUrl_;
    QUrl webSocketUrl_;

    QString code_;
    QString customerToken_;
    QString supporterToken_;
    QString deviceId_;

    quint64 nextRequestId_ = 1;
};
