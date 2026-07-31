#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

typedef struct _GstElement GstElement;

class CustomerVoiceAudio final : public QObject
{
    Q_OBJECT

public:
    explicit CustomerVoiceAudio(
        QObject *parent = nullptr);

    ~CustomerVoiceAudio() override;

    bool startCustomerSender(
        const QString &providerHost,
        std::uint16_t providerPort,
        const QString &inputNode);

    bool startProviderReceiver(
        std::uint16_t listenPort,
        const QString &outputNode);

    void stop();

    bool isRunning() const;

signals:
    void statusChanged(
        const QString &status);

    void errorOccurred(
        const QString &message);

private:
    static bool initializeGStreamer();

    static QString quotePipelineValue(
        const QString &value);

    bool startPipeline(
        GstElement *pipeline,
        const QString &description);

    void stopPipeline(
        GstElement *&pipeline);

    GstElement *senderPipeline_ = nullptr;
    GstElement *receiverPipeline_ = nullptr;
};
