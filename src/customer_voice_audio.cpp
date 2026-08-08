#include "customer_voice_audio.h"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <array>
#include <cstring>
#include <mutex>

#include <QMetaObject>
#include <QThread>

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
    if (QThread::currentThread() != thread()) {
        bool started = false;

        QMetaObject::invokeMethod(
            this,
            [
                this,
                inputNode,
                &started
            ]()
            {
                started =
                    startPacketSender(
                        inputNode);
            },
            Qt::BlockingQueuedConnection);

        return started;
    }

    stopSender();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    /*
     * The same APM instance will later receive the
     * remote render reference. Do not recreate it
     * when the second side of the voice path starts.
     */
    {
        std::lock_guard<std::mutex> lock(
            audioProcessorMutex_);

        if (
            !audioProcessor_.isInitialized() &&
            !audioProcessor_.initialize()
        ) {
            emit errorOccurred(
                QStringLiteral(
                    "WebRTC AEC3 could not be initialized."));
            return false;
        }
    }

    capturePcmBuffer_.clear();
    capturePcmTimestampNs_ = 0;

    QString source =
        QStringLiteral("pulsesrc");

    if (!inputNode.trimmed().isEmpty()) {
        source +=
            QStringLiteral(" device=%1")
                .arg(
                    quotePipelineValue(
                        inputNode.trimmed()));
    }

    /*
     * This pipeline deliberately contains two
     * disconnected GStreamer chains.
     *
     * Chain 1 captures raw microphone PCM and exposes
     * it through an appsink.
     *
     * CustomerVoiceAudio processes that PCM through
     * WebRTC AEC3 in exact 10 ms blocks and pushes
     * the processed PCM into Chain 2 through appsrc.
     *
     * Chain 2 preserves the existing volume, Opus,
     * RTP, and voicePacketReady transport.
     */
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
            "channels=1,"
            "layout=interleaved "
            "! appsink "
            "name=voice-capture-pcm-sink "
            "emit-signals=true "
            "sync=false "
            "max-buffers=8 "
            "drop=false "
            "appsrc "
            "name=voice-capture-pcm-source "
            "is-live=true "
            "format=time "
            "do-timestamp=false "
            "block=false "
            "caps=\"audio/x-raw,"
            "format=S16LE,"
            "rate=48000,"
            "channels=1,"
            "layout=interleaved\" "
            "! queue "
            "max-size-time=200000000 "
            "leaky=downstream "
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
            "async=false "
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

    GstElement *captureSink =
        gst_bin_get_by_name(
            GST_BIN(senderPipeline_),
            "voice-capture-pcm-sink");

    capturePcmAppSource_ =
        gst_bin_get_by_name(
            GST_BIN(senderPipeline_),
            "voice-capture-pcm-source");

    GstElement *packetSink =
        gst_bin_get_by_name(
            GST_BIN(senderPipeline_),
            "voice-packet-sink");

    if (
        captureSink == nullptr ||
        capturePcmAppSource_ == nullptr ||
        packetSink == nullptr
    ) {
        if (captureSink != nullptr) {
            gst_object_unref(captureSink);
        }

        if (packetSink != nullptr) {
            gst_object_unref(packetSink);
        }

        emit errorOccurred(
            QStringLiteral(
                "The WAN voice PCM bridge "
                "could not be created."));

        stopSender();

        return false;
    }

    g_signal_connect(
        captureSink,
        "new-sample",
        G_CALLBACK((
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
                    self->capturePcmBuffer_.append(
                        reinterpret_cast<
                            const char *>(
                                map.data),
                        static_cast<int>(
                            map.size));

                    gst_buffer_unmap(
                        buffer,
                        &map);
                }

                gst_sample_unref(sample);

                constexpr int pcmBlockBytes =
                    AssistAudioProcessor::
                        samplesPerBlock *
                    static_cast<int>(
                        sizeof(int16_t));

                while (
                    self->capturePcmBuffer_.size() >=
                    pcmBlockBytes
                ) {
                    std::array<
                        int16_t,
                        AssistAudioProcessor::
                            samplesPerBlock>
                        input{};

                    std::array<
                        int16_t,
                        AssistAudioProcessor::
                            samplesPerBlock>
                        output{};

                    std::memcpy(
                        input.data(),
                        self->
                            capturePcmBuffer_.
                            constData(),
                        pcmBlockBytes);

                    self->capturePcmBuffer_.remove(
                        0,
                        pcmBlockBytes);

                    bool processed = false;

                    {
                        std::lock_guard<std::mutex>
                            lock(
                                self->
                                    audioProcessorMutex_);

                        processed =
                            self->
                                audioProcessor_.
                                processCapture(
                                    input.data(),
                                    output.data());
                    }

                    if (!processed) {
                        emit self->errorOccurred(
                            QStringLiteral(
                                "AEC3 microphone "
                                "processing failed."));

                        return GST_FLOW_ERROR;
                    }
                    static bool capturePeakReported = false;
                    if (!capturePeakReported) {
                        int inputPeak = 0;
                        int outputPeak = 0;
                        for (size_t i = 0; i < input.size(); ++i) {
                            int a = input[i] < 0 ? -static_cast<int>(input[i]) : static_cast<int>(input[i]);
                            int b = output[i] < 0 ? -static_cast<int>(output[i]) : static_cast<int>(output[i]);
                            if (a > inputPeak) inputPeak = a;
                            if (b > outputPeak) outputPeak = b;
                        }
                        g_printerr("VOICE DEBUG: capture input peak=%d output peak=%d\n", inputPeak, outputPeak);
                        capturePeakReported = true;
                    }


                    GstBuffer *processedBuffer =
                        gst_buffer_new_allocate(
                            nullptr,
                            pcmBlockBytes,
                            nullptr);

                    if (processedBuffer == nullptr) {
                        return GST_FLOW_ERROR;
                    }

                    gst_buffer_fill(
                        processedBuffer,
                        0,
                        output.data(),
                        pcmBlockBytes);

                    GST_BUFFER_PTS(
                        processedBuffer) =
                            self->
                                capturePcmTimestampNs_;

                    GST_BUFFER_DURATION(
                        processedBuffer) =
                            10 * GST_MSECOND;

                    self->capturePcmTimestampNs_ +=
                        10 * GST_MSECOND;

                    const GstFlowReturn result =
                        gst_app_src_push_buffer(
                            GST_APP_SRC(
                                self->
                                    capturePcmAppSource_),
                            processedBuffer);

                    if (
                        result != GST_FLOW_OK &&
                        result != GST_FLOW_FLUSHING
                    ) {
                        emit self->errorOccurred(
                            QStringLiteral(
                                "AEC3 microphone PCM "
                                "output stopped."));

                        return result;
                    }

                    if (
                        result ==
                        GST_FLOW_FLUSHING
                    ) {
                        return result;
                    }
                }

                return GST_FLOW_OK;
            })),
        this);

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

    gst_object_unref(captureSink);
    gst_object_unref(packetSink);

    if (!startPipeline(
            senderPipeline_,
            QStringLiteral(
                "Voice packet sender"))) {
        stopSender();

        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "AEC3 microphone packet "
            "transmission is active."));

    return true;
}

bool CustomerVoiceAudio::startPacketReceiver(
    const QString &outputNode)
{
    if (QThread::currentThread() != thread()) {
        bool started = false;

        QMetaObject::invokeMethod(
            this,
            [
                this,
                outputNode,
                &started
            ]()
            {
                started =
                    startPacketReceiver(
                        outputNode);
            },
            Qt::BlockingQueuedConnection);

        return started;
    }

    stopReceiver();

    if (!initializeGStreamer()) {
        emit errorOccurred(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(
            audioProcessorMutex_);

        if (
            !audioProcessor_.isInitialized() &&
            !audioProcessor_.initialize()
        ) {
            emit errorOccurred(
                QStringLiteral(
                    "WebRTC AEC3 could not be initialized."));
            return false;
        }
    }

    renderPcmBuffer_.clear();
    renderPcmTimestampNs_ = 0;

    QString sink =
        QStringLiteral("pulsesink");

    if (!outputNode.trimmed().isEmpty()) {
        sink +=
            QStringLiteral(" device=%1")
                .arg(
                    quotePipelineValue(
                        outputNode.trimmed()));
    }

    /*
     * Chain 1 receives RTP/Opus and exposes decoded
     * 48 kHz mono PCM through an appsink.
     *
     * The PCM is fed to WebRTC ProcessReverseStream()
     * in exact 10 ms blocks.
     *
     * Chain 2 receives the processed render PCM and
     * sends it to the selected output device.
     */
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
            "! audio/x-raw,"
            "format=S16LE,"
            "rate=48000,"
            "channels=1,"
            "layout=interleaved "
            "! appsink "
            "name=voice-render-pcm-sink "
            "emit-signals=true "
            "sync=false "
            "max-buffers=8 "
            "drop=false "
            "appsrc "
            "name=voice-render-pcm-source "
            "is-live=true "
            "format=time "
            "do-timestamp=false "
            "block=false "
            "caps=\"audio/x-raw,"
            "format=S16LE,"
            "rate=48000,"
            "channels=1,"
            "layout=interleaved\" "
            "! queue "
            "max-size-time=200000000 "
            "leaky=downstream "
            "! %1 "
            "sync=false "
            "async=false")
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

    GstElement *renderSink =
        gst_bin_get_by_name(
            GST_BIN(receiverPipeline_),
            "voice-render-pcm-sink");

    renderPcmAppSource_ =
        gst_bin_get_by_name(
            GST_BIN(receiverPipeline_),
            "voice-render-pcm-source");

    if (
        receiverAppSource_ == nullptr ||
        renderSink == nullptr ||
        renderPcmAppSource_ == nullptr
    ) {
        if (renderSink != nullptr) {
            gst_object_unref(renderSink);
        }

        emit errorOccurred(
            QStringLiteral(
                "The WAN voice render PCM bridge "
                "could not be created."));

        stopReceiver();

        return false;
    }

    g_signal_connect(
        renderSink,
        "new-sample",
        G_CALLBACK((
            +[](
                GstAppSink *sink,
                gpointer userData)
                -> GstFlowReturn
            {
                auto *self =
                    static_cast<CustomerVoiceAudio *>(
                        userData);

                static std::once_flag renderReported;

                std::call_once(
                    renderReported,
                    []()
                    {
                        g_printerr(
                            "VOICE DEBUG: decoded PCM callback reached\n");
                    });

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
                    self->renderPcmBuffer_.append(
                        reinterpret_cast<
                            const char *>(
                                map.data),
                        static_cast<int>(
                            map.size));

                    gst_buffer_unmap(
                        buffer,
                        &map);
                }

                gst_sample_unref(sample);

                constexpr int pcmBlockBytes =
                    AssistAudioProcessor::
                        samplesPerBlock *
                    static_cast<int>(
                        sizeof(int16_t));

                while (
                    self->renderPcmBuffer_.size() >=
                    pcmBlockBytes
                ) {
                    std::array<
                        int16_t,
                        AssistAudioProcessor::
                            samplesPerBlock>
                        input{};

                    std::array<
                        int16_t,
                        AssistAudioProcessor::
                            samplesPerBlock>
                        output{};

                    std::memcpy(
                        input.data(),
                        self->
                            renderPcmBuffer_.
                            constData(),
                        pcmBlockBytes);

                    self->renderPcmBuffer_.remove(
                        0,
                        pcmBlockBytes);

                    bool processed = false;

                    {
                        std::lock_guard<std::mutex>
                            lock(
                                self->
                                    audioProcessorMutex_);

                        processed =
                            self->
                                audioProcessor_.
                                processRender(
                                    input.data(),
                                    output.data());
                    }

                    if (!processed) {
                        emit self->errorOccurred(
                            QStringLiteral(
                                "AEC3 render "
                                "processing failed."));

                        return GST_FLOW_ERROR;
                    }

                    static std::once_flag aecRenderReported;

                std::call_once(
                    aecRenderReported,
                    []()
                    {
                        g_printerr(
                            "VOICE DEBUG: AEC3 render succeeded\n");
                    });

                GstBuffer *processedBuffer =
                        gst_buffer_new_allocate(
                            nullptr,
                            pcmBlockBytes,
                            nullptr);

                    if (processedBuffer == nullptr) {
                        return GST_FLOW_ERROR;
                    }

                    gst_buffer_fill(
                        processedBuffer,
                        0,
                        input.data(),
                        pcmBlockBytes);

                    GST_BUFFER_PTS(
                        processedBuffer) =
                            self->
                                renderPcmTimestampNs_;

                    GST_BUFFER_DURATION(
                        processedBuffer) =
                            10 * GST_MSECOND;

                    self->renderPcmTimestampNs_ +=
                        10 * GST_MSECOND;

                    const GstFlowReturn result =
                        gst_app_src_push_buffer(
                            GST_APP_SRC(
                                self->
                                    renderPcmAppSource_),
                            processedBuffer);

                    if (
                        result != GST_FLOW_OK &&
                        result != GST_FLOW_FLUSHING
                    ) {
                        emit self->errorOccurred(
                            QStringLiteral(

                                "AEC3 render PCM "
                                "output stopped."));

                        return result;
                    }

                    if (
                        result ==
                        GST_FLOW_FLUSHING
                    ) {
                        return result;
                    }
                    static bool outputPushReported = false;
                    if (!outputPushReported) {
                        outputPushReported = true;
                        g_printerr("VOICE DEBUG: render PCM pushed to output\n");
                    }

                }

                return GST_FLOW_OK;
            })),
        this);

    gst_object_unref(renderSink);

    if (!startPipeline(
            receiverPipeline_,
            QStringLiteral(
                "Voice packet receiver"))) {
        stopReceiver();
        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "AEC3 voice packet playback is active."));

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

    static std::once_flag receiveReported;

    std::call_once(
        receiveReported,
        []()
        {
            g_printerr(
                "VOICE DEBUG: pushVoicePacket reached\n");
        });

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

void CustomerVoiceAudio::stopSender()
{
    stopPipeline(
        senderPipeline_);

    if (capturePcmAppSource_ != nullptr) {
        gst_object_unref(
            capturePcmAppSource_);

        capturePcmAppSource_ = nullptr;
    }

    capturePcmBuffer_.clear();
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

    if (renderPcmAppSource_ != nullptr) {
        gst_object_unref(
            renderPcmAppSource_);

        renderPcmAppSource_ = nullptr;
    }

    renderPcmBuffer_.clear();
}

void CustomerVoiceAudio::stop()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this]()
            {
                stop();
            },
            Qt::BlockingQueuedConnection);

        return;
    }

    const bool wasRunning =
        isRunning();

    stopSender();
    stopReceiver();

    {
        std::lock_guard<std::mutex> lock(
            audioProcessorMutex_);

        audioProcessor_.reset();
    }

    capturePcmTimestampNs_ = 0;
    renderPcmTimestampNs_ = 0;

    if (wasRunning) {
        emit statusChanged(
            QStringLiteral(
                "Voice stopped."));
    }
}

void CustomerVoiceAudio::setMuted(
    bool muted)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [
                this,
                muted
            ]()
            {
                setMuted(muted);
            },
            Qt::BlockingQueuedConnection);

        return;
    }

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
