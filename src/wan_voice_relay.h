#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;

class WanVoiceRelay final : public QObject
{
    Q_OBJECT

public:
    explicit WanVoiceRelay(
        QObject *parent = nullptr);

    ~WanVoiceRelay() override;

public slots:
    void connectForSession(
        const QUrl &webSocketUrl,
        const QString &code,
        const QString &role,
        const QString &token,
        const QString &deviceId);

    void disconnectFromServer();

    void sendVoicePacket(
        const QByteArray &packet);

signals:
    void voicePacketReceived(
        const QByteArray &packet);

    void readyChanged(
        bool ready);

    void statusChanged(
        const QString &status);

private:
    void processTextMessage(
        const QString &message);

    void sendJson(
        const QString &type,
        const QJsonObject &additionalFields = {});

    void sendSubscription();

    void setReady(
        bool ready);

    QWebSocket *socket_ = nullptr;

    QUrl webSocketUrl_;

    QString code_;
    QString role_;
    QString token_;
    QString deviceId_;

    bool ready_ = false;

    quint64 nextRequestId_ = 1;
};
