#include "portal_session.h"

#include <QDebug>
#include <QDBusArgument>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QProcessEnvironment>
#include <QStringList>
#include <QUuid>
#include <QVariant>

#include <poll.h>
#include <unistd.h>

namespace
{
constexpr auto portalService =
    "org.freedesktop.portal.Desktop";

constexpr auto portalPath =
    "/org/freedesktop/portal/desktop";

constexpr auto remoteDesktopInterface =
    "org.freedesktop.portal.RemoteDesktop";

constexpr auto screenCastInterface =
    "org.freedesktop.portal.ScreenCast";

constexpr auto clipboardInterface =
    "org.freedesktop.portal.Clipboard";

constexpr auto requestInterface =
    "org.freedesktop.portal.Request";

constexpr auto sessionInterface =
    "org.freedesktop.portal.Session";

constexpr uint keyboardDevice = 1;
constexpr uint pointerDevice = 2;

constexpr uint monitorSource = 1;
constexpr uint embeddedCursor = 2;

QString deviceDescription(uint devices)
{
    QStringList names;

    if ((devices & keyboardDevice) != 0) {
        names.append(QStringLiteral("keyboard"));
    }

    if ((devices & pointerDevice) != 0) {
        names.append(QStringLiteral("pointer"));
    }

    if ((devices & 4U) != 0) {
        names.append(QStringLiteral("touchscreen"));
    }

    if (names.isEmpty()) {
        return QStringLiteral("none");
    }

    return names.join(QStringLiteral(", "));
}
}

QDBusArgument &operator<<(
    QDBusArgument &argument,
    const PortalStream &stream)
{
    argument.beginStructure();
    argument << stream.nodeId;
    argument << stream.properties;
    argument.endStructure();

    return argument;
}

const QDBusArgument &operator>>(
    const QDBusArgument &argument,
    PortalStream &stream)
{
    argument.beginStructure();
    argument >> stream.nodeId;
    argument >> stream.properties;
    argument.endStructure();

    return argument;
}

PortalSession::PortalSession(QObject *parent)
    : QObject(parent),
      bus_(QDBusConnection::sessionBus())
{
    qDBusRegisterMetaType<PortalStream>();
    qDBusRegisterMetaType<PortalStreamList>();
}

PortalSession::~PortalSession()
{
    stop();
}

bool PortalSession::isSupported() const
{
    const QString sessionType =
        QProcessEnvironment::systemEnvironment()
            .value(QStringLiteral("XDG_SESSION_TYPE"))
            .trimmed()
            .toLower();

    if (sessionType != QStringLiteral("wayland")) {
        return false;
    }

    if (!bus_.isConnected()) {
        return false;
    }

    QDBusInterface remoteDesktop(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(remoteDesktopInterface),
        bus_);

    QDBusInterface screenCast(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(screenCastInterface),
        bus_);

    return remoteDesktop.isValid() &&
           screenCast.isValid();
}

bool PortalSession::isActive() const
{
    return stage_ == Stage::Active;
}

QString PortalSession::newToken(
    const QString &prefix) const
{
    QString token =
        QUuid::createUuid()
            .toString(QUuid::WithoutBraces);

    token.remove(QLatin1Char('-'));

    return prefix + QLatin1Char('_') + token;
}

QString PortalSession::requestPath(
    const QString &token) const
{
    QString sender = bus_.baseService();

    if (sender.startsWith(QLatin1Char(':'))) {
        sender.remove(0, 1);
    }

    sender.replace(QLatin1Char('.'), QLatin1Char('_'));

    return QStringLiteral(
               "/org/freedesktop/portal/desktop/request/")
        + sender
        + QLatin1Char('/')
        + token;
}

bool PortalSession::connectRequest(
    const QString &path,
    Stage stage)
{
    disconnectRequest();

    const bool connected =
        bus_.connect(
            QString::fromLatin1(portalService),
            path,
            QString::fromLatin1(requestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(onRequestResponse(uint,QVariantMap)));

    if (!connected) {
        fail(
            QStringLiteral(
                "Could not listen for the portal response."));
        return false;
    }

    currentRequestPath_ = path;
    stage_ = stage;

    return true;
}

void PortalSession::disconnectRequest()
{
    if (currentRequestPath_.isEmpty()) {
        return;
    }

    bus_.disconnect(
        QString::fromLatin1(portalService),
        currentRequestPath_,
        QString::fromLatin1(requestInterface),
        QStringLiteral("Response"),
        this,
        SLOT(onRequestResponse(uint,QVariantMap)));

    currentRequestPath_.clear();
}

bool PortalSession::callRequestMethod(
    const QString &interfaceName,
    const QString &methodName,
    const QVariantList &arguments,
    const QString &requestToken,
    Stage stage)
{
    const QString expectedPath =
        requestPath(requestToken);

    if (!connectRequest(expectedPath, stage)) {
        return false;
    }

    QDBusInterface portal(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        interfaceName,
        bus_);

    if (!portal.isValid()) {
        disconnectRequest();

        fail(
            QStringLiteral("Portal interface unavailable: ")
            + interfaceName);

        return false;
    }

    const QDBusMessage reply =
        portal.callWithArgumentList(
            QDBus::Block,
            methodName,
            arguments);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        disconnectRequest();

        fail(
            QStringLiteral("%1 failed: %2")
                .arg(methodName, reply.errorMessage()));

        return false;
    }

    if (!reply.arguments().isEmpty()) {
        const QDBusObjectPath returnedPath =
            qvariant_cast<QDBusObjectPath>(
                reply.arguments().constFirst());

        if (!returnedPath.path().isEmpty() &&
            returnedPath.path() != expectedPath) {
            disconnectRequest();

            if (!connectRequest(
                    returnedPath.path(),
                    stage)) {
                return false;
            }
        }
    }

    return true;
}

void PortalSession::start()
{
    if (!isSupported()) {
        fail(
            QStringLiteral(
                "A Wayland session with ScreenCast and "
                "RemoteDesktop portals is required."));
        return;
    }

    if (stage_ != Stage::Idle) {
        return;
    }

    emit busyChanged(true);
    emit activeChanged(false);
    emit detailsChanged(QString());

    createSession();
}

void PortalSession::createSession()
{
    emit statusChanged(
        QStringLiteral("Creating portal session…"));

    const QString requestToken =
        newToken(QStringLiteral("create"));

    const QString sessionToken =
        newToken(QStringLiteral("session"));

    QVariantMap options;
    options.insert(
        QStringLiteral("handle_token"),
        requestToken);

    options.insert(
        QStringLiteral("session_handle_token"),
        sessionToken);

    callRequestMethod(
        QString::fromLatin1(remoteDesktopInterface),
        QStringLiteral("CreateSession"),
        QVariantList{options},
        requestToken,
        Stage::CreatingSession);
}

void PortalSession::selectDevices()
{
    emit statusChanged(
        QStringLiteral(
            "Requesting keyboard and pointer access…"));

    const QString requestToken =
        newToken(QStringLiteral("devices"));

    QVariantMap options;
    options.insert(
        QStringLiteral("handle_token"),
        requestToken);

    options.insert(
        QStringLiteral("types"),
        QVariant::fromValue(
            keyboardDevice | pointerDevice));

    options.insert(
        QStringLiteral("persist_mode"),
        QVariant::fromValue(0U));

    callRequestMethod(
        QString::fromLatin1(remoteDesktopInterface),
        QStringLiteral("SelectDevices"),
        QVariantList{
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            options
        },
        requestToken,
        Stage::SelectingDevices);
}

void PortalSession::selectSources()
{
    emit statusChanged(
        QStringLiteral("Requesting one monitor…"));

    const QString requestToken =
        newToken(QStringLiteral("sources"));

    QVariantMap options;
    options.insert(
        QStringLiteral("handle_token"),
        requestToken);

    options.insert(
        QStringLiteral("types"),
        QVariant::fromValue(monitorSource));

    options.insert(
        QStringLiteral("multiple"),
        false);

    options.insert(
        QStringLiteral("cursor_mode"),
        QVariant::fromValue(embeddedCursor));

    callRequestMethod(
        QString::fromLatin1(screenCastInterface),
        QStringLiteral("SelectSources"),
        QVariantList{
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            options
        },
        requestToken,
        Stage::SelectingSources);
}

bool PortalSession::requestClipboard()
{
    emit statusChanged(
        QStringLiteral(
            "Requesting clipboard access…"));

    QDBusInterface clipboard(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QStringLiteral(
            "org.freedesktop.portal.Clipboard"),
        bus_);

    if (!clipboard.isValid()) {
        fail(
            QStringLiteral(
                "The Clipboard portal interface "
                "is unavailable."));
        return false;
    }

    const QDBusMessage reply =
        clipboard.call(
            QStringLiteral(
                "RequestClipboard"),
            QVariant::fromValue(
                QDBusObjectPath(
                    sessionHandle_)),
            QVariantMap());

    if (reply.type() ==
        QDBusMessage::ErrorMessage) {
        fail(
            QStringLiteral(
                "RequestClipboard failed: %1")
                .arg(
                    reply.errorMessage()));
        return false;
    }

    qInfo()
        << "portal: clipboard requested";

    return true;
}

void PortalSession::startSession()
{
    emit statusChanged(
        QStringLiteral(
            "Waiting for local approval and monitor selection…"));

    const QString requestToken =
        newToken(QStringLiteral("start"));

    QVariantMap options;
    options.insert(
        QStringLiteral("handle_token"),
        requestToken);

    callRequestMethod(
        QString::fromLatin1(remoteDesktopInterface),
        QStringLiteral("Start"),
        QVariantList{
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            QString(),
            options
        },
        requestToken,
        Stage::Starting);
}

void PortalSession::onRequestResponse(
    uint response,
    const QVariantMap &results)
{
    const Stage completedStage = stage_;

    disconnectRequest();

    if (response != 0) {
        if (response == 1) {
            fail(
                QStringLiteral(
                    "The portal request was cancelled."));
        } else {
            fail(
                QStringLiteral(
                    "The portal request failed with response %1.")
                    .arg(response));
        }

        return;
    }

    switch (completedStage) {
    case Stage::CreatingSession:
        sessionHandle_ =
            sessionHandleFrom(results);

        if (sessionHandle_.isEmpty()) {
            fail(
                QStringLiteral(
                    "The portal did not return a session handle."));
            return;
        }

        bus_.connect(
            QString::fromLatin1(portalService),
            sessionHandle_,
            QString::fromLatin1(sessionInterface),
            QStringLiteral("Closed"),
            this,
            SLOT(onSessionClosed()));

        selectDevices();
        break;

    case Stage::SelectingDevices:
        selectSources();
        break;

    case Stage::SelectingSources:
        if (!requestClipboard()) {
            return;
        }

        startSession();
        break;

    case Stage::Starting: {
        selectedDevices_ =
            results.value(
                QStringLiteral("devices"))
                .toUInt();

        const bool clipboardEnabled =
            results.value(
                QStringLiteral(
                    "clipboard_enabled"))
                .toBool();

        qInfo()
            << "portal: clipboard enabled"
            << clipboardEnabled;

        if (!clipboardEnabled) {
            fail(
                QStringLiteral(
                    "The portal started the session "
                    "without clipboard access."));
            return;
        }

        streams_ = streamsFrom(results);

        if (streams_.isEmpty()) {
            fail(
                QStringLiteral(
                    "The portal started the session but "
                    "returned no screen streams."));
            return;
        }

        qInfo() << "portal: Start completed";

        if (!connectClipboardSignals()) {
            return;
        }

        if (!openPipeWireRemote()) {
            return;
        }

        qInfo() << "portal: PipeWire remote opened"
                << pipeWireFd_;

        if (!connectToEis()) {
            return;
        }

        qInfo() << "portal: EIS connection opened"
                << eisFd_;

        stage_ = Stage::Active;

        emit pipeWireStreamReady(
            pipeWireFd_,
            streams_.constFirst().nodeId);

        emit eisConnectionReady(
            eisFd_);

        emit statusChanged(
            QStringLiteral(
                "Wayland remote-desktop session active"));

        emit detailsChanged(
            QStringLiteral(
                "Session handle:\n%1\n\n"
                "Selected devices: %2 (mask %3)\n\n"
                "PipeWire remote FD: %4\n\n"
                "%5")
                .arg(
                    sessionHandle_,
                    deviceDescription(selectedDevices_),
                    QString::number(selectedDevices_),
                    QString::number(pipeWireFd_),
                    describeStreams(streams_)));

        emit busyChanged(false);
        emit activeChanged(true);
        break;
    }

    case Stage::Idle:
    case Stage::Active:
        fail(
            QStringLiteral(
                "Unexpected portal response."));
        break;
    }
}

QString PortalSession::sessionHandleFrom(
    const QVariantMap &results) const
{
    const QVariant value =
        results.value(QStringLiteral("session_handle"));

    QString handle = value.toString();

    if (!handle.isEmpty()) {
        return handle;
    }

    return qvariant_cast<QDBusObjectPath>(value).path();
}

PortalStreamList PortalSession::streamsFrom(
    const QVariantMap &results) const
{
    const QVariant value =
        results.value(QStringLiteral("streams"));

    if (!value.isValid()) {
        return {};
    }

    return qdbus_cast<PortalStreamList>(value);
}

QString PortalSession::describeStreams(
    const PortalStreamList &streams) const
{
    QStringList descriptions;

    for (qsizetype index = 0;
         index < streams.size();
         ++index) {
        const PortalStream &stream = streams.at(index);

        QString description =
            QStringLiteral(
                "Stream %1\n"
                "  PipeWire node ID: %2")
                .arg(
                    index + 1)
                .arg(stream.nodeId);

        const QVariant sourceType =
            stream.properties.value(
                QStringLiteral("source_type"));

        if (sourceType.isValid()) {
            description +=
                QStringLiteral(
                    "\n  Source type: %1")
                    .arg(sourceType.toUInt());
        }

        const QVariant mappingId =
            stream.properties.value(
                QStringLiteral("mapping_id"));

        if (mappingId.isValid()) {
            description +=
                QStringLiteral(
                    "\n  Mapping ID: %1")
                    .arg(mappingId.toString());
        }

        const QVariant streamId =
            stream.properties.value(
                QStringLiteral("id"));

        if (streamId.isValid()) {
            description +=
                QStringLiteral(
                    "\n  Stream ID: %1")
                    .arg(streamId.toString());
        }

        descriptions.append(description);
    }

    return descriptions.join(QStringLiteral("\n\n"));
}

bool PortalSession::openPipeWireRemote()
{
    QDBusInterface screenCast(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(screenCastInterface),
        bus_);

    const QDBusReply<QDBusUnixFileDescriptor> reply =
        screenCast.call(
            QStringLiteral("OpenPipeWireRemote"),
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            QVariantMap());

    if (!reply.isValid()) {
        fail(
            QStringLiteral(
                "OpenPipeWireRemote failed: %1")
                .arg(reply.error().message()));
        return false;
    }

    const int receivedFd =
        reply.value().fileDescriptor();

    if (receivedFd < 0) {
        fail(
            QStringLiteral(
                "The portal returned an invalid PipeWire FD."));
        return false;
    }

    pipeWireFd_ = ::dup(receivedFd);

    if (pipeWireFd_ < 0) {
        fail(
            QStringLiteral(
                "Could not retain the PipeWire FD."));
        return false;
    }

    return true;
}

bool PortalSession::connectToEis()
{
    QDBusInterface remoteDesktop(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(remoteDesktopInterface),
        bus_);

    const QDBusReply<QDBusUnixFileDescriptor> reply =
        remoteDesktop.call(
            QStringLiteral("ConnectToEIS"),
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            QVariantMap());

    if (!reply.isValid()) {
        fail(
            QStringLiteral(
                "ConnectToEIS failed: %1")
                .arg(reply.error().message()));
        return false;
    }

    const int receivedFd =
        reply.value().fileDescriptor();

    if (receivedFd < 0) {
        fail(
            QStringLiteral(
                "The portal returned an invalid EIS FD."));
        return false;
    }

    eisFd_ = ::dup(receivedFd);

    if (eisFd_ < 0) {
        fail(
            QStringLiteral(
                "Could not retain the EIS FD."));
        return false;
    }

    return true;
}

bool PortalSession::connectClipboardSignals()
{
    if (clipboardSignalsConnected_) {
        return true;
    }

    const bool ownerConnected =
        bus_.connect(
            QString::fromLatin1(portalService),
            QString::fromLatin1(portalPath),
            QString::fromLatin1(clipboardInterface),
            QStringLiteral("SelectionOwnerChanged"),
            this,
            SLOT(onSelectionOwnerChanged(
                QDBusObjectPath,QVariantMap)));

    const bool transferConnected =
        bus_.connect(
            QString::fromLatin1(portalService),
            QString::fromLatin1(portalPath),
            QString::fromLatin1(clipboardInterface),
            QStringLiteral("SelectionTransfer"),
            this,
            SLOT(onSelectionTransfer(
                QDBusObjectPath,QString,uint)));

    if (!ownerConnected ||
        !transferConnected) {
        disconnectClipboardSignals();

        fail(
            QStringLiteral(
                "Could not subscribe to portal "
                "clipboard events."));

        return false;
    }

    clipboardSignalsConnected_ = true;

    qInfo()
        << "portal: clipboard signals connected";

    return true;
}

void PortalSession::disconnectClipboardSignals()
{
    bus_.disconnect(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(clipboardInterface),
        QStringLiteral("SelectionOwnerChanged"),
        this,
        SLOT(onSelectionOwnerChanged(
            QDBusObjectPath,QVariantMap)));

    bus_.disconnect(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(clipboardInterface),
        QStringLiteral("SelectionTransfer"),
        this,
        SLOT(onSelectionTransfer(
            QDBusObjectPath,QString,uint)));

    clipboardSignalsConnected_ = false;
}

QString PortalSession::readClipboardText(
    const QString &mimeType,
    bool *ok)
{
    *ok = false;

    QDBusInterface clipboard(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(clipboardInterface),
        bus_);

    const QDBusReply<QDBusUnixFileDescriptor> reply =
        clipboard.call(
            QStringLiteral("SelectionRead"),
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            mimeType);

    if (!reply.isValid()) {
        emit statusChanged(
            QStringLiteral(
                "Clipboard read failed: ")
            + reply.error().message());

        return {};
    }

    const int receivedFd =
        reply.value().fileDescriptor();

    if (receivedFd < 0) {
        return {};
    }

    const int retainedFd =
        ::dup(receivedFd);

    if (retainedFd < 0) {
        return {};
    }

    QFile file;

    if (!file.open(
            retainedFd,
            QIODevice::ReadOnly,
            QFileDevice::AutoCloseHandle)) {
        ::close(retainedFd);
        return {};
    }

    constexpr qint64 maximumSize =
        1024 * 1024;

    struct pollfd pollFd = {};
    pollFd.fd = retainedFd;
    pollFd.events =
        POLLIN |
        POLLHUP |
        POLLERR;

    const int pollResult =
        ::poll(
            &pollFd,
            1,
            1500);

    if (pollResult <= 0 ||
        (pollFd.revents &
         (POLLIN | POLLHUP)) == 0) {
        file.close();
        return {};
    }

    const QByteArray data =
        file.read(
            maximumSize + 1);

    file.close();

    if (data.isEmpty()) {
        return {};
    }

    if (data.size() > maximumSize) {
        emit statusChanged(
            QStringLiteral(
                "Clipboard text exceeded the "
                "1 MiB limit."));

        return {};
    }

    *ok = true;

    return QString::fromUtf8(data);
}

void PortalSession::onSelectionOwnerChanged(
    const QDBusObjectPath &sessionHandle,
    const QVariantMap &options)
{
    if (stage_ != Stage::Active ||
        sessionHandle.path() !=
            sessionHandle_) {
        return;
    }

    const bool sessionIsOwner =
        options.value(
            QStringLiteral(
                "session_is_owner"))
            .toBool();

    if (sessionIsOwner) {
        return;
    }

    const QStringList mimeTypes =
        options.value(
            QStringLiteral("mime_types"))
            .toStringList();

    QString selectedMimeType;

    const QStringList preferredTypes = {
        QStringLiteral(
            "text/plain;charset=utf-8"),
        QStringLiteral("text/plain"),
        QStringLiteral("UTF8_STRING")
    };

    for (const QString &preferred :
         preferredTypes) {
        if (mimeTypes.contains(
                preferred,
                Qt::CaseInsensitive)) {
            selectedMimeType = preferred;
            break;
        }
    }

    if (selectedMimeType.isEmpty() &&
        mimeTypes.isEmpty()) {
        selectedMimeType =
            QStringLiteral(
                "text/plain;charset=utf-8");
    }

    if (selectedMimeType.isEmpty()) {
        return;
    }

    bool ok = false;

    const QString text =
        readClipboardText(
            selectedMimeType,
            &ok);

    if (!ok ||
        text == clipboardText_) {
        return;
    }

    clipboardText_ = text;

    qInfo()
        << "portal: local clipboard changed"
        << "length=" << text.size();

    emit clipboardTextChanged(text);
}

void PortalSession::setClipboardText(
    const QString &text)
{
    if (stage_ != Stage::Active ||
        text == clipboardText_) {
        return;
    }

    if (text.toUtf8().size() >
        1024 * 1024) {
        emit statusChanged(
            QStringLiteral(
                "Remote clipboard text exceeded "
                "the 1 MiB limit."));
        return;
    }

    clipboardText_ = text;

    QVariantMap options;

    options.insert(
        QStringLiteral("mime_types"),
        QStringList{
            QStringLiteral(
                "text/plain;charset=utf-8"),
            QStringLiteral("text/plain")
        });

    QDBusInterface clipboard(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(clipboardInterface),
        bus_);

    const QDBusMessage reply =
        clipboard.call(
            QStringLiteral("SetSelection"),
            QVariant::fromValue(
                QDBusObjectPath(sessionHandle_)),
            options);

    if (reply.type() ==
        QDBusMessage::ErrorMessage) {
        emit statusChanged(
            QStringLiteral(
                "Clipboard selection failed: ")
            + reply.errorMessage());
        return;
    }

    qInfo()
        << "portal: remote clipboard offered"
        << "length=" << text.size();
}

void PortalSession::onSelectionTransfer(
    const QDBusObjectPath &sessionHandle,
    const QString &mimeType,
    uint serial)
{
    if (stage_ != Stage::Active ||
        sessionHandle.path() !=
            sessionHandle_) {
        return;
    }

    const bool supportedMimeType =
        mimeType.compare(
            QStringLiteral(
                "text/plain;charset=utf-8"),
            Qt::CaseInsensitive) == 0 ||
        mimeType.compare(
            QStringLiteral("text/plain"),
            Qt::CaseInsensitive) == 0;

    QDBusInterface clipboard(
        QString::fromLatin1(portalService),
        QString::fromLatin1(portalPath),
        QString::fromLatin1(clipboardInterface),
        bus_);

    bool success = false;

    if (supportedMimeType) {
        const QDBusReply<QDBusUnixFileDescriptor> reply =
            clipboard.call(
                QStringLiteral("SelectionWrite"),
                QVariant::fromValue(
                    QDBusObjectPath(
                        sessionHandle_)),
                serial);

        if (reply.isValid()) {
            const int receivedFd =
                reply.value().fileDescriptor();

            const int retainedFd =
                receivedFd >= 0
                    ? ::dup(receivedFd)
                    : -1;

            if (retainedFd >= 0) {
                QFile file;

                if (file.open(
                        retainedFd,
                        QIODevice::WriteOnly,
                        QFileDevice::AutoCloseHandle)) {
                    const QByteArray data =
                        clipboardText_.toUtf8();

                    qint64 totalWritten = 0;

                    while (
                        totalWritten <
                        data.size()) {
                        const qint64 written =
                            file.write(
                                data.constData()
                                    + totalWritten,
                                data.size()
                                    - totalWritten);

                        if (written <= 0) {
                            break;
                        }

                        totalWritten += written;
                    }

                    file.close();

                    success =
                        totalWritten ==
                        data.size();
                } else {
                    ::close(retainedFd);
                }
            }
        }
    }

    clipboard.call(
        QDBus::NoBlock,
        QStringLiteral(
            "SelectionWriteDone"),
        QVariant::fromValue(
            QDBusObjectPath(sessionHandle_)),
        serial,
        success);

    qInfo()
        << "portal: clipboard transfer"
        << mimeType
        << "success=" << success;
}

void PortalSession::stop()
{
    if (stage_ == Stage::Idle) {
        return;
    }

    emit statusChanged(
        QStringLiteral("Stopping portal session…"));

    disconnectRequest();
    closeSessionObject();
    resetState();

    emit statusChanged(
        QStringLiteral("Session stopped"));

    emit detailsChanged(QString());
    emit busyChanged(false);
    emit activeChanged(false);
}

void PortalSession::closeSessionObject()
{
    if (sessionHandle_.isEmpty()) {
        return;
    }

    bus_.disconnect(
        QString::fromLatin1(portalService),
        sessionHandle_,
        QString::fromLatin1(sessionInterface),
        QStringLiteral("Closed"),
        this,
        SLOT(onSessionClosed()));

    QDBusInterface session(
        QString::fromLatin1(portalService),
        sessionHandle_,
        QString::fromLatin1(sessionInterface),
        bus_);

    if (session.isValid()) {
        session.call(
            QDBus::NoBlock,
            QStringLiteral("Close"));
    }
}

void PortalSession::onSessionClosed()
{
    disconnectRequest();
    resetState();

    emit statusChanged(
        QStringLiteral(
            "The desktop closed the portal session"));

    emit detailsChanged(QString());
    emit busyChanged(false);
    emit activeChanged(false);
}

void PortalSession::resetState()
{
    disconnectClipboardSignals();
    clipboardText_.clear();

    if (pipeWireFd_ >= 0) {
        ::close(pipeWireFd_);
        pipeWireFd_ = -1;
    }

    if (eisFd_ >= 0) {
        ::close(eisFd_);
        eisFd_ = -1;
    }

    stage_ = Stage::Idle;
    currentRequestPath_.clear();
    sessionHandle_.clear();
    selectedDevices_ = 0;
    streams_.clear();
}

void PortalSession::fail(const QString &message)
{
    closeSessionObject();
    resetState();

    emit statusChanged(
        QStringLiteral("Error: ") + message);

    emit busyChanged(false);
    emit activeChanged(false);
}

