#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

typedef struct _GstElement GstElement;

class RemoteDesktopAudio final : public QObject
{
    Q_OBJECT

public:
    explicit RemoteDesktopAudio(
        QObject *parent = nullptr);

    ~RemoteDesktopAudio() override;

    bool startSender();

    bool startReceiver(
        const QString &outputNode);

    void stopSender();
    void stopReceiver();
    void stop();

    bool isSenderRunning() const;
    bool isReceiverRunning() const;

public slots:
    void pushAudioPacket(
        const QByteArray &packet);

signals:
    void audioPacketReady(
        const QByteArray &packet);

    void statusChanged(
        const QString &status);

    void errorOccurred(
        const QString &message);

private:
    static bool initializeGStreamer();

    bool startPipeline(
        GstElement *pipeline,
        const QString &description);

    void stopPipeline(
        GstElement *&pipeline);

    GstElement *senderPipeline_ = nullptr;
    GstElement *receiverPipeline_ = nullptr;
    GstElement *receiverAppSource_ = nullptr;
};
