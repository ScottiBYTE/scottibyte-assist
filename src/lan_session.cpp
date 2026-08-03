#include "lan_session.h"
#include "desktop_backend.h"

#include <QBuffer>
#include <QDateTime>
#include <QDataStream>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>


namespace
{

QString normalizedCode(
    QString code)
{
    code.remove(' ');
    return code.trimmed();
}

QByteArray pointPayload(
    int x,
    int y)
{
    QByteArray payload;

    QDataStream stream(
        &payload,
        QIODevice::WriteOnly);

    stream.setByteOrder(
        QDataStream::BigEndian);

    stream
        << static_cast<qint32>(x)
        << static_cast<qint32>(y);

    return payload;
}

QByteArray keyPayload(
    int qtKey)
{
    QByteArray payload;

    QDataStream stream(
        &payload,
        QIODevice::WriteOnly);

    stream.setByteOrder(
        QDataStream::BigEndian);

    stream
        << static_cast<qint32>(
               qtKey);

    return payload;
}

bool decodeKey(
    const QByteArray &payload,
    int &qtKey)
{
    if (payload.size() !=
        static_cast<int>(
            sizeof(qint32))) {
        return false;
    }

    QDataStream stream(payload);

    stream.setByteOrder(
        QDataStream::BigEndian);

    qint32 decodedKey = 0;
    stream >> decodedKey;

    qtKey = decodedKey;
    return true;
}

bool decodePoint(
    const QByteArray &payload,
    int &x,
    int &y)
{
    if (payload.size() !=
        static_cast<int>(
            sizeof(qint32) * 2)) {
        return false;
    }

    QDataStream stream(payload);

    stream.setByteOrder(
        QDataStream::BigEndian);

    qint32 decodedX = 0;
    qint32 decodedY = 0;

    stream
        >> decodedX
        >> decodedY;

    x = decodedX;
    y = decodedY;

    return true;
}

}

LanSession::LanSession(
    QObject *parent)
    : QObject(parent),
      discoverySocket_(
          new QUdpSocket(this)),
      tcpServer_(
          new QTcpServer(this)),
      advertiseTimer_(
          new QTimer(this))
{
    advertiseTimer_->setInterval(
        750);

    connect(
        advertiseTimer_,
        &QTimer::timeout,
        this,
        &LanSession::advertiseSession);

    connect(
        discoverySocket_,
        &QUdpSocket::readyRead,
        this,
        &LanSession::processDiscoveryDatagrams);

    connect(
        tcpServer_,
        &QTcpServer::newConnection,
        this,
        &LanSession::acceptProvider);
}

LanSession::~LanSession()
{
    disconnectSession();
}

void LanSession::setDesktopBackend(
    DesktopBackend *backend)
{
    if (desktopBackend_ == backend) {
        return;
    }

    if (desktopBackend_ != nullptr) {
        disconnect(
            desktopBackend_,
            nullptr,
            this,
            nullptr);
    }

    desktopBackend_ = backend;

    if (desktopBackend_ == nullptr) {
        return;
    }

    connect(
        desktopBackend_,
        &DesktopBackend::frameReady,
        this,
        &LanSession::sendDesktopFrame);

    connect(
        desktopBackend_,
        &DesktopBackend::statusChanged,
        this,
        &LanSession::statusChanged);

    connect(
        desktopBackend_,
        &DesktopBackend::errorOccurred,
        this,
        &LanSession::errorOccurred);
}

void LanSession::startCustomer(
    const QString &code)
{
    disconnectSession();

    code_ = normalizedCode(code);

    if (code_.size() != 6) {
        emit errorOccurred(
            QStringLiteral(
                "The support code is invalid."));
        return;
    }

    role_ = Role::Customer;

    if (!tcpServer_->listen(
            QHostAddress::AnyIPv4,
            sessionPort_)) {
        emit errorOccurred(
            QStringLiteral(
                "Could not start the LAN support listener: ")
            + tcpServer_->errorString());

        role_ = Role::Inactive;
        return;
    }

    startAdvertising();

    emit statusChanged(
        QStringLiteral(
            "Waiting for the person helping you..."));
}

void LanSession::startAdvertising()
{
    advertiseTimer_->start();
    advertiseSession();
}

void LanSession::advertiseSession()
{
    if (role_ != Role::Customer ||
        !tcpServer_->isListening()) {
        return;
    }

    const QByteArray message =
        QStringLiteral(
            "SCOTTIBYTE_ASSIST_V1 %1 %2")
            .arg(code_)
            .arg(sessionPort_)
            .toUtf8();

    const QList<QNetworkInterface> interfaces =
        QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &interface :
         interfaces) {
        if (!(interface.flags() &
              QNetworkInterface::IsUp) ||
            !(interface.flags() &
              QNetworkInterface::IsRunning) ||
            (interface.flags() &
             QNetworkInterface::IsLoopBack)) {
            continue;
        }

        for (const QNetworkAddressEntry &entry :
             interface.addressEntries()) {
            if (entry.ip().protocol() !=
                QAbstractSocket::IPv4Protocol) {
                continue;
            }

            const QHostAddress broadcast =
                entry.broadcast();

            if (!broadcast.isNull()) {
                discoverySocket_->writeDatagram(
                    message,
                    broadcast,
                    discoveryPort_);
            }
        }
    }
}

void LanSession::connectProvider(
    const QString &code)
{
    disconnectSession();

    code_ = normalizedCode(code);

    if (code_.size() != 6) {
        emit errorOccurred(
            QStringLiteral(
                "Enter all six digits."));
        return;
    }

    role_ = Role::Provider;

    if (!discoverySocket_->bind(
            QHostAddress::AnyIPv4,
            discoveryPort_,
            QUdpSocket::ShareAddress |
            QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(
            QStringLiteral(
                "Could not listen for the support computer: ")
            + discoverySocket_->errorString());

        role_ = Role::Inactive;
        return;
    }

    emit statusChanged(
        QStringLiteral(
            "Looking for the support computer on the LAN..."));
}

void LanSession::connectProviderDirect(
    const QString &code,
    const QString &address,
    quint16 port)
{
    disconnectSession();

    code_ = normalizedCode(code);

    if (code_.size() != 6) {
        emit errorOccurred(
            QStringLiteral(
                "Enter all six digits."));
        return;
    }

    const QString trimmedAddress =
        address.trimmed();

    if (trimmedAddress.isEmpty() ||
        port == 0) {
        emit errorOccurred(
            QStringLiteral(
                "The direct support address is invalid."));
        return;
    }

    role_ = Role::Provider;

    connectToCustomer(
        trimmedAddress,
        port);
}

void LanSession::processDiscoveryDatagrams()
{
    while (
        discoverySocket_->hasPendingDatagrams()) {
        const QNetworkDatagram datagram =
            discoverySocket_
                ->receiveDatagram();

        const QString message =
            QString::fromUtf8(
                datagram.data());

        const QStringList fields =
            message.split(
                QChar(' '),
                Qt::SkipEmptyParts);

        if (fields.size() != 3 ||
            fields.at(0) !=
                QStringLiteral(
                    "SCOTTIBYTE_ASSIST_V1") ||
            fields.at(1) != code_) {
            continue;
        }

        bool validPort = false;

        const quint16 port =
            fields.at(2).toUShort(
                &validPort);

        if (!validPort) {
            continue;
        }

        discoverySocket_->close();

        connectToCustomer(
            datagram.senderAddress()
                .toString(),
            port);

        return;
    }
}

void LanSession::connectToCustomer(
    const QString &address,
    quint16 port)
{
    emit statusChanged(
        QStringLiteral(
            "Connecting to the support computer..."));

    auto *socket =
        new QTcpSocket(this);

    attachSocket(socket);

    socket->connectToHost(
        address,
        port);
}

QString LanSession::peerAddress() const
{
    if (socket_ == nullptr) {
        return {};
    }

    return socket_->
        peerAddress()
        .toString();
}

QStringList LanSession::customerCandidateAddresses() const
{
    QStringList addresses;

    if (
        role_ != Role::Customer ||
        !tcpServer_->isListening()
    ) {
        return addresses;
    }

    const QList<QNetworkInterface> interfaces =
        QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &interface :
         interfaces) {
        if (
            !(interface.flags() &
              QNetworkInterface::IsUp) ||
            !(interface.flags() &
              QNetworkInterface::IsRunning) ||
            (interface.flags() &
             QNetworkInterface::IsLoopBack)
        ) {
            continue;
        }

        const QString interfaceName =
            interface.name().toLower();

        if (
            interfaceName.startsWith(
                QStringLiteral("docker")) ||
            interfaceName.startsWith(
                QStringLiteral("veth")) ||
            interfaceName.startsWith(
                QStringLiteral("virbr")) ||
            interfaceName.startsWith(
                QStringLiteral("incusbr")) ||
            interfaceName.startsWith(
                QStringLiteral("lxdbr")) ||
            interfaceName.startsWith(
                QStringLiteral("br-"))
        ) {
            continue;
        }

        for (const QNetworkAddressEntry &entry :
             interface.addressEntries()) {
            const QHostAddress address =
                entry.ip();

            if (
                address.protocol() !=
                    QAbstractSocket::IPv4Protocol ||
                address.isLoopback()
            ) {
                continue;
            }

            const QString addressText =
                address.toString();

            if (
                addressText.startsWith(
                    QStringLiteral("169.254."))
            ) {
                continue;
            }

            if (!addresses.contains(addressText)) {
                addresses.append(addressText);
            }
        }
    }

    return addresses;
}

quint16 LanSession::customerSessionPort() const
{
    return sessionPort_;
}

bool LanSession::isConnected() const
{
    return
        relayActive_ ||
        (
            socket_ != nullptr &&
            socket_->state() ==
                QAbstractSocket::ConnectedState
        );
}

QString LanSession::diagnosticSummary() const
{
    const QString transport =
        relayActive_
            ? QStringLiteral(
                  "Assist WAN relay")
            : (
                socket_ != nullptr
                    ? QStringLiteral(
                          "Direct LAN TCP")
                    : QStringLiteral(
                          "Disconnected")
            );

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

    case Role::Provider:
        roleText =
            QStringLiteral("Provider");
        break;
    }

    QString lastFrameText =
        QStringLiteral("Never");

    if (lastDesktopFrameReceivedMs_ > 0) {
        const qint64 elapsed =
            QDateTime::
                currentMSecsSinceEpoch() -
            lastDesktopFrameReceivedMs_;

        lastFrameText =
            QStringLiteral("%1 seconds ago")
                .arg(
                    static_cast<double>(
                        elapsed) /
                        1000.0,
                    0,
                    'f',
                    1);
    }

    return QStringLiteral(
        "Desktop transport\n"
        "Role: %1\n"
        "Transport: %2\n"
        "Connected: %3\n"
        "Relay queued bytes: %4\n"
        "Relay bytes received: %5\n"
        "Frames prepared: %6\n"
        "Frames sent: %7\n"
        "Frames dropped for backlog: %8\n"
        "Frames received: %9\n"
        "Last frame received: %10")
        .arg(
            roleText,
            transport,
            isConnected()
                ? QStringLiteral("Yes")
                : QStringLiteral("No"),
            QString::number(
                relayBytesQueued_),
            QString::number(
                relayBytesReceived_),
            QString::number(
                desktopFramesPrepared_),
            QString::number(
                desktopFramesSent_),
            QString::number(
                desktopFramesDropped_),
            QString::number(
                desktopFramesReceived_),
            lastFrameText);
}


void LanSession::activateRelayTransport()
{
    if (
        role_ == Role::Inactive ||
        code_.isEmpty() ||
        relayActive_
    ) {
        return;
    }

    advertiseTimer_->stop();
    discoverySocket_->close();
    tcpServer_->close();

    if (socket_ != nullptr) {
        QTcpSocket *socket =
            socket_;

        socket_ = nullptr;

        socket->disconnect(
            this);

        socket->abort();
        socket->deleteLater();
    }

    receiveBuffer_.clear();
    expectedPayloadSize_ = 0;
    relayBytesQueued_ = 0;

    relayBytesReceived_ = 0;
    desktopFramesPrepared_ = 0;
    desktopFramesSent_ = 0;
    desktopFramesDropped_ = 0;
    desktopFramesReceived_ = 0;
    lastDesktopFrameReceivedMs_ = 0;

    relayActive_ = true;

    if (
        role_ == Role::Customer &&
        desktopBackend_ != nullptr
    ) {
        desktopBackend_->start();
    }

    emit statusChanged(
        role_ == Role::Customer
            ? QStringLiteral(
                  "The person helping you is connected "
                  "through the Assist relay.")
            : QStringLiteral(
                  "Connected to the support computer "
                  "through the Assist relay."));

    emit connectedChanged(true);
}

void LanSession::receiveRelayBytes(
    const QByteArray &bytes)
{
    if (
        !relayActive_ ||
        bytes.isEmpty()
    ) {
        return;
    }

    relayBytesReceived_ +=
        static_cast<quint64>(
            bytes.size());

    processIncomingBytes(
        bytes);
}

void LanSession::setRelayBytesQueued(
    qint64 bytes)
{
    relayBytesQueued_ =
        qMax<qint64>(
            0,
            bytes);
}

void LanSession::relayTransportLost()
{
    if (!relayActive_) {
        return;
    }

    disconnectSession();

    emit statusChanged(
        QStringLiteral(
            "The Assist relay connection was lost."));
}

void LanSession::acceptProvider()
{
    if (socket_ != nullptr) {
        auto *extra =
            tcpServer_->nextPendingConnection();

        extra->disconnectFromHost();
        extra->deleteLater();
        return;
    }

    attachSocket(
        tcpServer_->nextPendingConnection());

    advertiseTimer_->stop();
    tcpServer_->close();

    if (desktopBackend_ != nullptr) {
        desktopBackend_->start();
    }

    emit statusChanged(
        QStringLiteral(
            "The person helping you is connected."));

    emit connectedChanged(true);
}

void LanSession::attachSocket(
    QTcpSocket *socket)
{
    relayActive_ = false;
    socket_ = socket;

    connect(
        socket_,
        &QTcpSocket::connected,
        this,
        [this]()
        {
            emit statusChanged(
                QStringLiteral(
                    "Connected to the support computer."));

            emit connectedChanged(true);
        });

    connect(
        socket_,
        &QTcpSocket::readyRead,
        this,
        &LanSession::processIncomingData);

    connect(
        socket_,
        &QTcpSocket::disconnected,
        this,
        [this]()
        {
            const bool providerShareWasActive =
                providerShareActive_;

            providerShareActive_ = false;

            if (desktopBackend_ != nullptr) {
                desktopBackend_->stop();
            }

            if (providerShareWasActive) {
                emit providerShareChanged(false);
            }

            emit statusChanged(
                QStringLiteral(
                    "Disconnected."));

            emit connectedChanged(false);

            if (socket_ != nullptr) {
                socket_->deleteLater();
                socket_ = nullptr;
            }
        });

    connect(
        socket_,
        &QTcpSocket::errorOccurred,
        this,
        [this](
            QAbstractSocket::SocketError)
        {
            emit errorOccurred(
                socket_ == nullptr
                    ? QStringLiteral(
                          "LAN connection error.")
                    : socket_->errorString());
        });
}

void LanSession::disconnectSession()
{
    advertiseTimer_->stop();

    const bool providerShareWasActive =
        providerShareActive_;

    providerShareActive_ = false;

    if (desktopBackend_ != nullptr) {
        desktopBackend_->stop();
    }

    if (providerShareWasActive) {
        emit providerShareChanged(false);
    }

    discoverySocket_->close();
    tcpServer_->close();

    receiveBuffer_.clear();
    expectedPayloadSize_ = 0;
    relayActive_ = false;
    relayBytesQueued_ = 0;

    if (socket_ != nullptr) {
        QTcpSocket *socket =
            socket_;

        socket_ = nullptr;

        socket->disconnect(
            this);

        socket->disconnectFromHost();

        if (socket->state() !=
            QAbstractSocket::UnconnectedState) {
            socket->abort();
        }

        socket->deleteLater();
    }

    role_ = Role::Inactive;
    code_.clear();

    emit connectedChanged(false);
}

void LanSession::startProviderShare()
{
    if (role_ != Role::Provider ||
        !isConnected() ||
        desktopBackend_ == nullptr ||
        providerShareActive_) {
        return;
    }

    providerShareActive_ = true;

    sendMessage(
        MessageType::ProviderShareState,
        QByteArray(1, '\1'));

    desktopBackend_->start();

    emit providerShareChanged(true);

    emit statusChanged(
        QStringLiteral(
            "Sharing the provider desktop."));
}

void LanSession::stopProviderShare()
{
    if (!providerShareActive_) {
        return;
    }

    providerShareActive_ = false;

    sendMessage(
        MessageType::ProviderShareState,
        QByteArray(1, '\0'));

    if (desktopBackend_ != nullptr) {
        desktopBackend_->stop();
    }

    emit providerShareChanged(false);

    emit statusChanged(
        QStringLiteral(
            "Provider desktop sharing stopped."));
}

void LanSession::sendDesktopFrame(
    const QImage &sourceImage)
{
    if (
        !isConnected() ||
        sourceImage.isNull()
    ) {
        return;
    }

    ++desktopFramesPrepared_;

    const qint64 queuedBytes =
        relayActive_
            ? relayBytesQueued_
            : (
                socket_ != nullptr
                    ? socket_->bytesToWrite()
                    : 0
            );

    if (
        queuedBytes >
        frameBacklogLimit_
    ) {
        ++desktopFramesDropped_;
        return;
    }

    MessageType messageType =
        MessageType::Frame;

    if (role_ == Role::Customer) {
        messageType =
            MessageType::Frame;
    } else if (
        role_ == Role::Provider &&
        providerShareActive_) {
        messageType =
            MessageType::ProviderFrame;
    } else {
        return;
    }

    QByteArray encoded;

    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);

    sourceImage.save(
        &buffer,
        "JPG",
        70);

    sendMessage(
        messageType,
        encoded);

    ++desktopFramesSent_;
}

void LanSession::notifyProviderScreenClosed()
{
    if (
        role_ != Role::Customer ||
        !isConnected()
    ) {
        return;
    }

    sendMessage(
        MessageType::ProviderScreenClosed,
        {});
}

void LanSession::requestVoiceStart()
{
    sendMessage(
        MessageType::VoiceStart,
        {});

    emit voiceStartRequested();
}

void LanSession::requestVoiceStop()
{
    sendMessage(
        MessageType::VoiceStop,
        {});

    emit voiceStopRequested();
}

void LanSession::sendVoicePacket(
    const QByteArray &packet)
{
    if (
        packet.isEmpty() ||
        packet.size() > 64 * 1024
    ) {
        return;
    }

    sendMessage(
        MessageType::VoicePacket,
        packet);
}

void LanSession::sendPointerMove(
    int x,
    int y)
{
    sendMessage(
        MessageType::PointerMove,
        pointPayload(x, y));
}

void LanSession::sendLeftClick(
    int x,
    int y)
{
    sendMessage(
        MessageType::LeftClick,
        pointPayload(x, y));
}

void LanSession::sendLeftButtonPress(
    int x,
    int y)
{
    sendMessage(
        MessageType::LeftButtonPress,
        pointPayload(x, y));
}

void LanSession::sendLeftButtonRelease(
    int x,
    int y)
{
    sendMessage(
        MessageType::LeftButtonRelease,
        pointPayload(x, y));
}

void LanSession::sendRightClick(
    int x,
    int y)
{
    sendMessage(
        MessageType::RightClick,
        pointPayload(x, y));
}

void LanSession::sendKeyPress(
    int qtKey)
{
    sendMessage(
        MessageType::KeyPress,
        keyPayload(qtKey));
}

void LanSession::sendKeyRelease(
    int qtKey)
{
    sendMessage(
        MessageType::KeyRelease,
        keyPayload(qtKey));
}

void LanSession::sendClipboardText(
    const QString &text)
{
    const QByteArray payload =
        text.toUtf8();

    if (payload.size() >
        1024 * 1024) {
        emit errorOccurred(
            QStringLiteral(
                "Clipboard text exceeds "
                "the 1 MiB limit."));
        return;
    }

    sendMessage(
        MessageType::ClipboardText,
        payload);
}

void LanSession::sendMessage(
    MessageType type,
    const QByteArray &payload)
{
    if (!isConnected()) {
        return;
    }

    QByteArray packet;

    QDataStream stream(
        &packet,
        QIODevice::WriteOnly);

    stream.setByteOrder(
        QDataStream::BigEndian);

    stream
        << static_cast<quint8>(type)
        << static_cast<quint32>(
               payload.size());

    packet.append(payload);

    if (relayActive_) {
        emit relayBytesReady(
            packet);
        return;
    }

    if (socket_ != nullptr) {
        socket_->write(packet);
    }
}

void LanSession::processIncomingData()
{
    if (socket_ == nullptr) {
        return;
    }

    processIncomingBytes(
        socket_->readAll());
}

void LanSession::processIncomingBytes(
    const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return;
    }

    receiveBuffer_.append(
        bytes);

    while (true) {
        if (expectedPayloadSize_ == 0) {
            constexpr int headerSize =
                sizeof(quint8) +
                sizeof(quint32);

            if (receiveBuffer_.size() <
                headerSize) {
                return;
            }

            QByteArray header =
                receiveBuffer_.left(
                    headerSize);

            receiveBuffer_.remove(
                0,
                headerSize);

            QDataStream stream(header);

            stream.setByteOrder(
                QDataStream::BigEndian);

            quint8 type = 0;

            stream
                >> type
                >> expectedPayloadSize_;

            expectedMessageType_ =
                static_cast<MessageType>(
                    type);

            if (expectedPayloadSize_ >
                16 * 1024 * 1024) {
                emit errorOccurred(
                    QStringLiteral(
                        "The LAN peer sent an invalid message."));

                disconnectSession();
                return;
            }
        }

        if (receiveBuffer_.size() <
            static_cast<int>(
                expectedPayloadSize_)) {
            return;
        }

        const QByteArray payload =
            receiveBuffer_.left(
                expectedPayloadSize_);

        receiveBuffer_.remove(
            0,
            expectedPayloadSize_);

        expectedPayloadSize_ = 0;

        if (
            expectedMessageType_ ==
                MessageType::Frame ||
            expectedMessageType_ ==
                MessageType::ProviderFrame) {
            QImage image;

            image.loadFromData(
                payload,
                "JPG");

            if (!image.isNull()) {
                ++desktopFramesReceived_;

                lastDesktopFrameReceivedMs_ =
                    QDateTime::
                        currentMSecsSinceEpoch();

                if (
                    expectedMessageType_ ==
                    MessageType::ProviderFrame) {
                    emit providerFrameReceived(
                        image);
                } else {
                    emit frameReceived(
                        image);
                }
            }

            continue;
        }

        if (expectedMessageType_ ==
            MessageType::ProviderScreenClosed) {
            if (role_ == Role::Provider) {
                stopProviderShare();
            }

            continue;
        }

        if (expectedMessageType_ ==
            MessageType::ClipboardText) {
            emit clipboardTextReceived(
                QString::fromUtf8(payload));

            continue;
        }

        if (expectedMessageType_ ==
            MessageType::ProviderShareState) {
            if (payload.size() == 1) {
                emit providerShareChanged(
                    payload.at(0) != 0);
            }

            continue;
        }

        if (expectedMessageType_ ==
            MessageType::VoiceStart) {
            emit voiceStartRequested();
            continue;
        }

        if (expectedMessageType_ ==
            MessageType::VoiceStop) {
            emit voiceStopRequested();
            continue;
        }

        if (expectedMessageType_ ==
            MessageType::VoicePacket) {
            if (!payload.isEmpty()) {
                emit voicePacketReceived(
                    payload);
            }

            continue;
        }

        if (desktopBackend_ == nullptr) {
            continue;
        }

        if (expectedMessageType_ ==
                MessageType::KeyPress ||
            expectedMessageType_ ==
                MessageType::KeyRelease) {
            int qtKey = 0;

            if (!decodeKey(
                    payload,
                    qtKey)) {
                continue;
            }

            if (expectedMessageType_ ==
                MessageType::KeyPress) {
                desktopBackend_->pressKey(
                    qtKey);
            } else {
                desktopBackend_->releaseKey(
                    qtKey);
            }

            continue;
        }

        int x = 0;
        int y = 0;

        if (!decodePoint(
                payload,
                x,
                y)) {
            continue;
        }

        if (expectedMessageType_ ==
            MessageType::PointerMove) {
            desktopBackend_->movePointerTo(
                x,
                y);
        } else if (
            expectedMessageType_ ==
            MessageType::LeftClick) {
            desktopBackend_->clickLeftAt(
                x,
                y);
        } else if (
            expectedMessageType_ ==
            MessageType::RightClick) {
            desktopBackend_->clickRightAt(
                x,
                y);
        } else if (
            expectedMessageType_ ==
            MessageType::LeftButtonPress) {
            desktopBackend_->pressLeftAt(
                x,
                y);
        } else if (
            expectedMessageType_ ==
            MessageType::LeftButtonRelease) {
            desktopBackend_->releaseLeftAt(
                x,
                y);
        }
    }
}
