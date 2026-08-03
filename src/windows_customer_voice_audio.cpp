#include "customer_voice_audio.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

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

bool CustomerVoiceAudio::startPacketSender(
    const QString &inputNode)
{
    stopSender();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    QString source =
        QStringLiteral("wasapi2src low-latency=true");

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
            "! volume "
            "name=voice-volume "
            "volume=%2 "
            "! opusenc "
            "bitrate=64000 "
            "audio-type=voice "
            "frame-size=20 "
            "inband-fec=true "
            "packet-loss-percentage=5 "
            "dtx=false "
            "! rtpopuspay "
            "pt=96 "
            "! appsink "
            "name=voice-packet-sink "
            "emit-signals=true "
            "sync=false "
            "max-buffers=8 "
            "drop=true")
            .arg(source)
            .arg(
                muted_
                    ? QStringLiteral("0.0")
                    : QStringLiteral("1.0"));

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
                "Could not create the voice "
                "packet sender: ") +
            errorText(error));

        g_error_free(error);

        if (senderPipeline_ != nullptr) {
            gst_object_unref(
                senderPipeline_);

            senderPipeline_ = nullptr;
        }

        return false;
    }

    GstElement *packetSink =
        gst_bin_get_by_name(
            GST_BIN(senderPipeline_),
            "voice-packet-sink");

    if (packetSink == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "The voice packet sink was not created."));

        stopPipeline(
            senderPipeline_);

        return false;
    }

    g_signal_connect(
        packetSink,
        "new-sample",
        G_CALLBACK(
            +[](
                GstAppSink *sink,
                gpointer userData)
                -> GstFlowReturn
            {
                auto *self =
                    static_cast<CustomerVoiceAudio *>(
                        userData);

                GstSample *sample =
                    gst_app_sink_pull_sample(
                        sink);

                if (sample == nullptr) {
                    return GST_FLOW_EOS;
                }

                GstBuffer *buffer =
                    gst_sample_get_buffer(
                        sample);

                GstMapInfo map{};

                if (
                    buffer != nullptr &&
                    gst_buffer_map(
                        buffer,
                        &map,
                        GST_MAP_READ)
                ) {
                    const QByteArray packet(
                        reinterpret_cast<
                            const char *>(
                                map.data),
                        static_cast<int>(
                            map.size));

                    gst_buffer_unmap(
                        buffer,
                        &map);

                    if (!packet.isEmpty()) {
                        emit self->
                            voicePacketReady(
                                packet);
                    }
                }

                gst_sample_unref(sample);

                return GST_FLOW_OK;
            }),
        this);

    gst_object_unref(packetSink);

    if (!startPipeline(
            senderPipeline_,
            QStringLiteral(
                "Voice packet sender"))) {
        stopPipeline(
            senderPipeline_);

        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Microphone packet transmission is active."));

    return true;
}

bool CustomerVoiceAudio::startPacketReceiver(
    const QString &outputNode)
{
    stopReceiver();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    QString sink =
        QStringLiteral("wasapi2sink low-latency=true");

    if (!outputNode.trimmed().isEmpty()) {
        sink +=
            QStringLiteral(" device=%1")
                .arg(
                    quotePipelineValue(
                        outputNode.trimmed()));
    }

    const QString pipelineDescription =
        QStringLiteral(
            "appsrc "
            "name=voice-packet-source "
            "is-live=true "
            "format=time "
            "do-timestamp=true "
            "block=false "
            "caps=\"application/x-rtp,"
            "media=audio,"
            "encoding-name=OPUS,"
            "payload=96,"
            "clock-rate=48000\" "
            "! queue "
            "max-size-time=200000000 "
            "leaky=downstream "
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
            "! %1 "
            "sync=false")
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
                "Could not create the voice "
                "packet receiver: ") +
            errorText(error));

        g_error_free(error);

        if (receiverPipeline_ != nullptr) {
            gst_object_unref(
                receiverPipeline_);

            receiverPipeline_ = nullptr;
        }

        return false;
    }

    receiverAppSource_ =
        gst_bin_get_by_name(
            GST_BIN(receiverPipeline_),
            "voice-packet-source");

    if (receiverAppSource_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "The voice packet source was not created."));

        stopPipeline(
            receiverPipeline_);

        return false;
    }

    if (!startPipeline(
            receiverPipeline_,
            QStringLiteral(
                "Voice packet receiver"))) {
        stopReceiver();
        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Voice packet playback is active."));

    return true;
}

void CustomerVoiceAudio::pushVoicePacket(
    const QByteArray &packet)
{
    if (
        receiverAppSource_ == nullptr ||
        packet.isEmpty()
    ) {
        return;
    }

    GstBuffer *buffer =
        gst_buffer_new_allocate(
            nullptr,
            static_cast<gsize>(
                packet.size()),
            nullptr);

    if (buffer == nullptr) {
        return;
    }

    gst_buffer_fill(
        buffer,
        0,
        packet.constData(),
        static_cast<gsize>(
            packet.size()));

    const GstFlowReturn result =
        gst_app_src_push_buffer(
            GST_APP_SRC(
                receiverAppSource_),
            buffer);

    if (
        result != GST_FLOW_OK &&
        result != GST_FLOW_FLUSHING
    ) {
        emit errorOccurred(
            QStringLiteral(
                "Voice packet playback stopped."));
    }
}

bool CustomerVoiceAudio::startCustomerSender(
    const QString &providerHost,
    std::uint16_t providerPort,
    const QString &inputNode)
{
    stopSender();

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
        QStringLiteral("wasapi2src low-latency=true");

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
            "! volume "
            "name=voice-volume "
            "volume=%2 "
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
            "host=%3 "
            "port=%4 "
            "sync=false "
            "async=false")
            .arg(source)
            .arg(
                muted_
                    ? QStringLiteral("0.0")
                    : QStringLiteral("1.0"))
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
    stopReceiver();

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
        QStringLiteral("wasapi2sink low-latency=true");

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

void CustomerVoiceAudio::stopSender()
{
    stopPipeline(
        senderPipeline_);
}

void CustomerVoiceAudio::stopReceiver()
{
    if (receiverAppSource_ != nullptr) {
        gst_object_unref(
            receiverAppSource_);

        receiverAppSource_ = nullptr;
    }

    stopPipeline(
        receiverPipeline_);
}

void CustomerVoiceAudio::stop()
{
    const bool wasRunning =
        isRunning();

    stopSender();
    stopReceiver();

    if (wasRunning) {
        emit statusChanged(
            QStringLiteral(
                "Voice stopped."));
    }
}

void CustomerVoiceAudio::setMuted(
    bool muted)
{
    muted_ = muted;

    if (senderPipeline_ == nullptr) {
        return;
    }

    GstElement *volume =
        gst_bin_get_by_name(
            GST_BIN(senderPipeline_),
            "voice-volume");

    if (volume == nullptr) {
        return;
    }

    g_object_set(
        volume,
        "volume",
        muted_
            ? 0.0
            : 1.0,
        nullptr);

    gst_object_unref(volume);
}

bool CustomerVoiceAudio::isRunning() const
{
    return
        senderPipeline_ != nullptr ||
        receiverPipeline_ != nullptr;
}

bool CustomerVoiceAudio::isMuted() const
{
    return muted_;
}
