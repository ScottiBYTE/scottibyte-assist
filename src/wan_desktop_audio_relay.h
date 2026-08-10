#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;

class WanDesktopAudioRelay final : public QObject
{
    Q_OBJECT

public:
    explicit WanDesktopAudioRelay(
        QObject *parent = nullptr);

    ~WanDesktopAudioRelay() override;

public slots:
    void connectForSession(
        const QUrl &webSocketUrl,
        const QString &code,
        const QString &role,
        const QString &token,
        const QString &deviceId);

    void disconnectFromServer();

    void sendAudioPacket(
        const QByteArray &packet);

signals:
    void audioPacketReceived(
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
