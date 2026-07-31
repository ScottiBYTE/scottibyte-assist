#include "customer_voice_audio.h"

#include <gst/gst.h>

#include <mutex>

namespace
{

std::once_flag initializationFlag;
bool gstreamerInitialized = false;

QString errorText(
    GError *error)
{
    if (error == nullptr) {
        return QStringLiteral(
            "Unknown GStreamer error.");
    }

    return QString::fromUtf8(
        error->message);
}

}

CustomerVoiceAudio::CustomerVoiceAudio(
    QObject *parent)
    : QObject(parent)
{
    initializeGStreamer();
}

CustomerVoiceAudio::~CustomerVoiceAudio()
{
    stop();
}

bool CustomerVoiceAudio::initializeGStreamer()
{
    std::call_once(
        initializationFlag,
        []()
        {
            GError *error = nullptr;

            gstreamerInitialized =
                gst_init_check(
                    nullptr,
                    nullptr,
                    &error);

            if (error != nullptr) {
                g_error_free(error);
            }
        });

    return gstreamerInitialized;
}

QString CustomerVoiceAudio::quotePipelineValue(
    const QString &value)
{
    QString escaped = value;

    escaped.replace(
        QChar('\\'),
        QStringLiteral("\\\\"));

    escaped.replace(
        QChar('"'),
        QStringLiteral("\\\""));

    return QStringLiteral("\"%1\"")
        .arg(escaped);
}

bool CustomerVoiceAudio::startPipeline(
    GstElement *pipeline,
    const QString &description)
{
    if (pipeline == nullptr) {
        return false;
    }

    const GstStateChangeReturn result =
        gst_element_set_state(
            pipeline,
            GST_STATE_PLAYING);

    if (result ==
        GST_STATE_CHANGE_FAILURE) {
        emit errorOccurred(
            description +
            QStringLiteral(
                " could not start."));

        gst_element_set_state(
            pipeline,
            GST_STATE_NULL);

        return false;
    }

    return true;
}

bool CustomerVoiceAudio::startCustomerSender(
    const QString &providerHost,
    std::uint16_t providerPort,
    const QString &inputNode)
{
    stop();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    if (
        providerHost.trimmed().isEmpty() ||
        providerPort == 0) {
        emit errorOccurred(
            QStringLiteral(
                "The provider voice destination is invalid."));
        return false;
    }

    QString source =
        QStringLiteral("pulsesrc");

    if (!inputNode.trimmed().isEmpty()) {
        source +=
            QStringLiteral(" device=%1")
                .arg(
                    quotePipelineValue(
                        inputNode.trimmed()));
    }

    const QString pipelineDescription =
        QStringLiteral(
            "%1 "
            "do-timestamp=true "
            "! queue "
            "max-size-time=200000000 "
            "leaky=downstream "
            "! audioconvert "
            "! audioresample "
            "! audio/x-raw,"
            "format=S16LE,"
            "rate=48000,"
            "channels=1 "
            "! opusenc "
            "bitrate=64000 "
            "audio-type=voice "
            "frame-size=20 "
            "inband-fec=true "
            "packet-loss-percentage=5 "
            "dtx=false "
            "! rtpopuspay "
            "pt=96 "
            "! udpsink "
            "host=%2 "
            "port=%3 "
            "sync=false "
            "async=false")
            .arg(source)
            .arg(
                quotePipelineValue(
                    providerHost.trimmed()))
            .arg(providerPort);

    GError *error = nullptr;

    senderPipeline_ =
        gst_parse_launch(
            pipelineDescription
                .toUtf8()
                .constData(),
            &error);

    if (error != nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not create the customer "
                "voice sender: ") +
            errorText(error));

        g_error_free(error);

        if (senderPipeline_ != nullptr) {
            gst_object_unref(
                senderPipeline_);

            senderPipeline_ = nullptr;
        }

        return false;
    }

    if (!startPipeline(
            senderPipeline_,
            QStringLiteral(
                "Customer voice sender"))) {
        stopPipeline(
            senderPipeline_);

        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Customer microphone transmission is active."));

    return true;
}

bool CustomerVoiceAudio::startProviderReceiver(
    std::uint16_t listenPort,
    const QString &outputNode)
{
    stop();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    if (listenPort == 0) {
        emit errorOccurred(
            QStringLiteral(
                "The provider voice listening port is invalid."));
        return false;
    }

    QString sink =
        QStringLiteral("pulsesink");

    if (!outputNode.trimmed().isEmpty()) {
        sink +=
            QStringLiteral(" device=%1")
                .arg(
                    quotePipelineValue(
                        outputNode.trimmed()));
    }

    const QString pipelineDescription =
        QStringLiteral(
            "udpsrc "
            "port=%1 "
            "caps=\"application/x-rtp,"
            "media=audio,"
            "encoding-name=OPUS,"
            "payload=96,"
            "clock-rate=48000\" "
            "! rtpjitterbuffer "
            "latency=40 "
            "drop-on-latency=true "
            "do-lost=true "
            "! rtpopusdepay "
            "! opusdec "
            "plc=true "
            "use-inband-fec=true "
            "! audioconvert "
            "! audioresample "
            "! %2 "
            "sync=false")
            .arg(listenPort)
            .arg(sink);

    GError *error = nullptr;

    receiverPipeline_ =
        gst_parse_launch(
            pipelineDescription
                .toUtf8()
                .constData(),
            &error);

    if (error != nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Could not create the provider "
                "voice receiver: ") +
            errorText(error));

        g_error_free(error);

        if (receiverPipeline_ != nullptr) {
            gst_object_unref(
                receiverPipeline_);

            receiverPipeline_ = nullptr;
        }

        return false;
    }

    if (!startPipeline(
            receiverPipeline_,
            QStringLiteral(
                "Provider voice receiver"))) {
        stopPipeline(
            receiverPipeline_);

        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Provider voice playback is active."));

    return true;
}

void CustomerVoiceAudio::stopPipeline(
    GstElement *&pipeline)
{
    if (pipeline == nullptr) {
        return;
    }

    gst_element_set_state(
        pipeline,
        GST_STATE_NULL);

    gst_object_unref(
        pipeline);

    pipeline = nullptr;
}

void CustomerVoiceAudio::stop()
{
    const bool wasRunning =
        isRunning();

    stopPipeline(
        senderPipeline_);

    stopPipeline(
        receiverPipeline_);

    if (wasRunning) {
        emit statusChanged(
            QStringLiteral(
                "Customer-to-provider voice stopped."));
    }
}

bool CustomerVoiceAudio::isRunning() const
{
    return
        senderPipeline_ != nullptr ||
        receiverPipeline_ != nullptr;
}
