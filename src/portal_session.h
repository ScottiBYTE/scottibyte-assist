#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

struct PortalStream
{
    uint nodeId = 0;
    QVariantMap properties;
};

using PortalStreamList = QList<PortalStream>;

Q_DECLARE_METATYPE(PortalStream)
Q_DECLARE_METATYPE(PortalStreamList)

class QDBusArgument;
class QDBusMessage;
class QTimer;
class QDBusObjectPath;

QDBusArgument &operator<<(
    QDBusArgument &argument,
    const PortalStream &stream);

const QDBusArgument &operator>>(
    const QDBusArgument &argument,
    PortalStream &stream);

class PortalSession final : public QObject
{
    Q_OBJECT

public:
    explicit PortalSession(QObject *parent = nullptr);
    ~PortalSession() override;

    bool isSupported() const;
    bool isActive() const;

public slots:
    void start();
    void stop();

    void setClipboardText(
        const QString &text);

signals:
    void statusChanged(const QString &status);
    void detailsChanged(const QString &details);
    void busyChanged(bool busy);
    void activeChanged(bool active);

    void pipeWireStreamReady(
        int fileDescriptor,
        uint nodeId);

    void pointerMappingReady(const QString &mappingId);

    void eisConnectionReady(
        int fileDescriptor);

    void clipboardTextChanged(
        const QString &text);

private slots:
    void onRequestResponse(
        uint response,
        const QVariantMap &results,
        const QDBusMessage &message);

    void onSessionClosed();

    void onSelectionOwnerChanged(
        const QDBusObjectPath &sessionHandle,
        const QVariantMap &options);

    void onSelectionTransfer(
        const QDBusObjectPath &sessionHandle,
        const QString &mimeType,
        uint serial);

private:
    enum class Stage
    {
        Idle,
        CreatingSession,
        SelectingDevices,
        SelectingSources,
        Starting,
        Active
    };

    QString newToken(const QString &prefix) const;
    QString requestPath(const QString &token) const;

    bool connectRequest(
        const QString &path,
        Stage stage);

    void disconnectRequest();
    void cancelPendingRequest();

    bool callRequestMethod(
        const QString &interfaceName,
        const QString &methodName,
        const QVariantList &arguments,
        const QString &requestToken,
        Stage stage);

    void createSession();
    void selectDevices();
    void selectSources();
    bool requestClipboard();
    void startSession();

    bool openPipeWireRemote();
    bool connectToEis();

    bool connectClipboardSignals();
    void disconnectClipboardSignals();
    QString readClipboardText(
        const QString &mimeType,
        bool *ok);

    void closeSessionObject();
    void resetState();

    QString sessionHandleFrom(
        const QVariantMap &results) const;

    PortalStreamList streamsFrom(
        const QVariantMap &results) const;

    QString describeStreams(
        const PortalStreamList &streams) const;

    void fail(const QString &message);

    QDBusConnection bus_;

    Stage stage_ = Stage::Idle;

    QTimer *requestTimer_ = nullptr;
    quint64 requestGeneration_ = 0;
    QString currentRequestMethod_;
    QString currentRequestPath_;
    QString sessionHandle_;

    int pipeWireFd_ = -1;
    int eisFd_ = -1;
    uint selectedDevices_ = 0;
    PortalStreamList streams_;

    bool clipboardSignalsConnected_ = false;
    QString clipboardText_;
};
