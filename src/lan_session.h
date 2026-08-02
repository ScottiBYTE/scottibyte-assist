#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class DesktopBackend;
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

    void setDesktopBackend(
        DesktopBackend *backend);

    void startCustomer(
        const QString &code);

    void connectProvider(
        const QString &code);

    void connectProviderDirect(
        const QString &code,
        const QString &address,
        quint16 port);

    void disconnectSession();

    QString peerAddress() const;

    void startProviderShare();
    void stopProviderShare();

    void notifyProviderScreenClosed();

    void requestVoiceStart();
    void requestVoiceStop();

    void sendPointerMove(
        int x,
        int y);

    void sendLeftClick(
        int x,
        int y);

    void sendLeftButtonPress(
        int x,
        int y);

    void sendLeftButtonRelease(
        int x,
        int y);

    void sendRightClick(
        int x,
        int y);

    void sendKeyPress(
        int qtKey);

    void sendKeyRelease(
        int qtKey);

    void sendClipboardText(
        const QString &text);

signals:
    void statusChanged(
        const QString &status);

    void connectedChanged(
        bool connected);

    void frameReceived(
        const QImage &image);

    void providerFrameReceived(
        const QImage &image);

    void providerShareChanged(
        bool active);

    void voiceStartRequested();
    void voiceStopRequested();

    void clipboardTextReceived(
        const QString &text);

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
        LeftClick = 3,
        RightClick = 4,
        LeftButtonPress = 5,
        LeftButtonRelease = 6,
        KeyPress = 7,
        KeyRelease = 8,
        ClipboardText = 9,
        ProviderFrame = 10,
        ProviderShareState = 11,
        ProviderScreenClosed = 12,
        VoiceStart = 13,
        VoiceStop = 14
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

    void sendDesktopFrame(
        const QImage &image);

    Role role_ = Role::Inactive;

    QString code_;

    QUdpSocket *discoverySocket_ = nullptr;
    QTcpServer *tcpServer_ = nullptr;
    QTcpSocket *socket_ = nullptr;

    QTimer *advertiseTimer_ = nullptr;

    DesktopBackend *desktopBackend_ = nullptr;

    bool providerShareActive_ = false;

    QByteArray receiveBuffer_;

    quint32 expectedPayloadSize_ = 0;
    MessageType expectedMessageType_ =
        MessageType::Frame;

    static constexpr quint16 discoveryPort_ =
        3092;

    static constexpr quint16 sessionPort_ =
        3093;
};
