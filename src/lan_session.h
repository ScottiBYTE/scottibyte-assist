#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;
class QUdpSocket;

class LanSession final : public QObject
{
    Q_OBJECT

public:
    explicit LanSession(
        QObject *parent = nullptr);

    ~LanSession() override;

    void startCustomer(
        const QString &code);

    void connectProvider(
        const QString &code);

    void disconnectSession();

    void sendPointerMove(
        int x,
        int y);

    void sendLeftClick(
        int x,
        int y);

signals:
    void statusChanged(
        const QString &status);

    void connectedChanged(
        bool connected);

    void frameReceived(
        const QImage &image);

    void errorOccurred(
        const QString &message);

private:
    enum class Role
    {
        Inactive,
        Customer,
        Provider
    };

    enum class MessageType : quint8
    {
        Frame = 1,
        PointerMove = 2,
        LeftClick = 3
    };

    void startAdvertising();
    void advertiseSession();
    void processDiscoveryDatagrams();

    void acceptProvider();
    void connectToCustomer(
        const QString &address,
        quint16 port);

    void attachSocket(
        QTcpSocket *socket);

    void processIncomingData();

    void sendMessage(
        MessageType type,
        const QByteArray &payload);

    void captureFrame();

    void applyPointerMove(
        int x,
        int y);

    void applyLeftClick(
        int x,
        int y);

    Role role_ = Role::Inactive;

    QString code_;

    QUdpSocket *discoverySocket_ = nullptr;
    QTcpServer *tcpServer_ = nullptr;
    QTcpSocket *socket_ = nullptr;

    QTimer *advertiseTimer_ = nullptr;
    QTimer *captureTimer_ = nullptr;

    QByteArray receiveBuffer_;

    quint32 expectedPayloadSize_ = 0;
    MessageType expectedMessageType_ =
        MessageType::Frame;

    static constexpr quint16 discoveryPort_ =
        3092;

    static constexpr quint16 sessionPort_ =
        3093;
};
