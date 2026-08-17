#pragma once

#include "assist_audio_processor.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <mutex>

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

    bool startPacketSender(
        const QString &inputNode);

    bool startPacketReceiver(
        const QString &outputNode);

    void pushVoicePacket(
        const QByteArray &packet);

    void stopSender();
    void stopReceiver();
    void stop();

    void setMuted(
        bool muted);

    bool isRunning() const;
    bool isMuted() const;

    static QString diagnosticSummary();

signals:
    void statusChanged(
        const QString &status);

    void errorOccurred(
        const QString &message);

    void voicePacketReady(
        const QByteArray &packet);

    void microphoneLevelChanged(
        float level);

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
    GstElement *receiverAppSource_ = nullptr;

    /*
     * Raw PCM bridge endpoints used by the WAN AEC3
     * packet path.
     */
    GstElement *capturePcmAppSource_ = nullptr;
    GstElement *renderPcmAppSource_ = nullptr;

    /*
     * GStreamer appsink buffers are not guaranteed to
     * arrive in WebRTC APM's required 10 ms size.
     * These accumulators let us extract exact
     * 480-sample / 960-byte blocks.
     */
    QByteArray capturePcmBuffer_;
    QByteArray renderPcmBuffer_;

    std::uint64_t capturePcmTimestampNs_ = 0;
    std::uint64_t renderPcmTimestampNs_ = 0;

    /*
     * Capture and render callbacks execute on separate
     * GStreamer streaming threads. Serialize all
     * access to the single WebRTC APM instance.
     */
    std::mutex audioProcessorMutex_;
    AssistAudioProcessor audioProcessor_;

    bool muted_ = false;
};
