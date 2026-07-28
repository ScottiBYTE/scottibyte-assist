#include "lan_session.h"
#include "desktop_backend.h"

#include <QBuffer>
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
            if (desktopBackend_ != nullptr) {
                desktopBackend_->stop();
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

    if (desktopBackend_ != nullptr) {
        desktopBackend_->stop();
    }

    discoverySocket_->close();
    tcpServer_->close();

    receiveBuffer_.clear();
    expectedPayloadSize_ = 0;

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

void LanSession::sendDesktopFrame(
    const QImage &sourceImage)
{
    if (role_ != Role::Customer ||
        socket_ == nullptr ||
        socket_->state() !=
            QAbstractSocket::ConnectedState ||
        sourceImage.isNull()) {
        return;
    }

    const QImage &image =
        sourceImage;

    QByteArray encoded;

    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);

    image.save(
        &buffer,
        "JPG",
        70);

    sendMessage(
        MessageType::Frame,
        encoded);
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
    if (socket_ == nullptr ||
        socket_->state() !=
            QAbstractSocket::ConnectedState) {
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

    socket_->write(packet);
}

void LanSession::processIncomingData()
{
    if (socket_ == nullptr) {
        return;
    }

    receiveBuffer_.append(
        socket_->readAll());

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

        if (expectedMessageType_ ==
            MessageType::Frame) {
            QImage image;

            image.loadFromData(
                payload,
                "JPG");

            if (!image.isNull()) {
                emit frameReceived(image);
            }

            continue;
        }

        if (expectedMessageType_ ==
            MessageType::ClipboardText) {
            emit clipboardTextReceived(
                QString::fromUtf8(payload));

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
