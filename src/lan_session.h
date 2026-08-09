#pragma once
#include <atomic>

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

class DesktopBackend;
class Vp8VideoCodec;
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

    QStringList customerCandidateAddresses() const;

    quint16 customerSessionPort() const;

    bool isConnected() const;

    QString diagnosticSummary() const;

    void activateRelayTransport();

    void receiveRelayBytes(
        const QByteArray &bytes);

    void setRelayBytesQueued(
        qint64 bytes);

    void relayTransportLost();

    void startProviderShare();
    void stopProviderShare();

    void notifyProviderScreenClosed();

    void requestRemoteControlDisplays();

    void requestRemoteControlStart(
        const QString &displayId);

    void requestRemoteControlStop();

    void requestVoiceStart();
    void requestVoiceStop();

    void sendVoicePacket(
        const QByteArray &packet);

    void receiveVoiceRelayPacket(
        const QByteArray &packet);

    void sendPointerMove(
        int x,
        int y);

    void sendRemoteCursorPosition(
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

    void remoteCursorPositionReceived(
        int x,
        int y);

    void providerFrameReceived(
        const QImage &image);

    void providerShareChanged(
        bool active);

    void remoteControlDisplaysReceived(
        const QStringList &displayIds,
        const QStringList &displayLabels);

    void voiceStartRequested();
    void voiceStopRequested();

    void voicePacketReceived(
        const QByteArray &packet);

    void voiceRelayPacketReady(
        const QByteArray &packet);

    void clipboardTextReceived(
        const QString &text);

    void relayBytesReady(
        const QByteArray &bytes);

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
        VoiceStop = 14,
        VoicePacket = 15,
        RemoteControlStart = 16,
        RemoteControlStop = 17,
        RemoteControlDisplaysRequest = 18,
        RemoteControlDisplaysResponse = 19,
        FrameAcknowledged = 20,
        ProviderFrameAcknowledged = 21,
        Vp8Frame = 22,
        ProviderVp8Frame = 23,
        RemoteCursorPosition = 24
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

    void processIncomingBytes(
        const QByteArray &bytes);

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
    Vp8VideoCodec *vp8VideoCodec_ = nullptr;

    bool providerShareActive_ = false;
    bool relayActive_ = false;
    bool waitingForRelayFrameAcknowledgement_ =
        false;

    qint64 relayBytesQueued_ = 0;

    quint64 relayBytesReceived_ = 0;
    quint64 desktopFramesPrepared_ = 0;
    quint64 desktopFramesSent_ = 0;
    quint64 desktopFramesDropped_ = 0;
    quint64 desktopFramesRateLimited_ = 0;
    quint64 desktopFramesReceived_ = 0;

    std::atomic_bool desktopEncodeInFlight_{false};

    quint64 voicePacketsSent_ = 0;
    quint64 voiceBytesSent_ = 0;
    quint64 voicePacketsReceived_ = 0;
    quint64 voiceBytesReceived_ = 0;

    qint64 lastDesktopFrameSubmittedMs_ = 0;
    qint64 lastDesktopFrameReceivedMs_ = 0;

    qint64 lastVoicePacketSentMs_ = 0;
    qint64 lastVoicePacketReceivedMs_ = 0;

    QByteArray receiveBuffer_;

    quint32 expectedPayloadSize_ = 0;
    MessageType expectedMessageType_ =
        MessageType::Frame;

    static constexpr quint16 discoveryPort_ =
        3092;

    static constexpr quint16 sessionPort_ =
        3093;

    static constexpr qint64 directFrameBacklogLimit_ =
        512 * 1024;

    static constexpr qint64 relayFrameBacklogLimit_ =
        128 * 1024;

    static constexpr qint64 relayFrameIntervalMs_ =
        67;

    static constexpr int directJpegQuality_ =
        70;

    static constexpr int relayJpegQuality_ =
        50;
};
