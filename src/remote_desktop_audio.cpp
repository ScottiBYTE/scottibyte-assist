#include "remote_desktop_audio.h"

#include <QProcess>
#include <QThread>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace
{
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

QString quotePipelineValue(
    const QString &value)
{
    QString escaped = value;

    escaped.replace(
        QChar('\\'),
        QStringLiteral("\\\\"));

    escaped.replace(
        QChar('"'),
        QStringLiteral("\\\""));

    return
        QStringLiteral("\"%1\"")
            .arg(escaped);
}

GstFlowReturn onDesktopAudioPacket(
    GstAppSink *sink,
    gpointer userData)
{
    auto *audio =
        static_cast<RemoteDesktopAudio *>(
            userData);

    GstSample *sample =
        gst_app_sink_pull_sample(
            sink);

    if (sample == nullptr) {
        return GST_FLOW_OK;
    }

    GstBuffer *buffer =
        gst_sample_get_buffer(
            sample);

    if (buffer == nullptr) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;

    if (
        gst_buffer_map(
            buffer,
            &map,
            GST_MAP_READ)
    ) {
        const QByteArray packet(
            reinterpret_cast<const char *>(
                map.data),
            static_cast<int>(
                map.size));

        if (!packet.isEmpty()) {
            emit audio->audioPacketReady(
                packet);
        }

        gst_buffer_unmap(
            buffer,
            &map);
    }

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}
}

RemoteDesktopAudio::RemoteDesktopAudio(
    QObject *parent)
    : QObject(parent)
{
}

RemoteDesktopAudio::~RemoteDesktopAudio()
{
    stop();
}

bool RemoteDesktopAudio::initializeGStreamer()
{
    static bool initialized = false;

    if (!initialized) {
        gst_init(
            nullptr,
            nullptr);

        initialized = true;
    }

    return true;
}

bool RemoteDesktopAudio::startPipeline(
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

    if (
        result ==
        GST_STATE_CHANGE_FAILURE)
    {
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

void RemoteDesktopAudio::stopPipeline(
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

QString RemoteDesktopAudio::defaultMonitorSource() const
{
    QProcess process;

    process.start(
        QStringLiteral("pactl"),
        {
            QStringLiteral("get-default-sink")
        });

    if (
        !process.waitForFinished(
            2000)
    ) {
        return QString();
    }

    const QString sink =
        QString::fromUtf8(
            process.readAllStandardOutput())
            .trimmed();

    if (sink.isEmpty()) {
        return QString();
    }

    return
        sink +
        QStringLiteral(".monitor");
}

bool RemoteDesktopAudio::startSender()
{
    if (
        QThread::currentThread() !=
        thread()
    ) {
        bool started = false;

        QMetaObject::invokeMethod(
            this,
            [
                this,
                &started
            ]()
            {
                started =
                    startSender();
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

    const QString monitor =
        defaultMonitorSource();

    if (monitor.isEmpty()) {
        emit errorOccurred(
            QStringLiteral(
                "No desktop audio monitor source "
                "could be found."));
        return false;
    }

    const QString pipelineDescription =
        QStringLiteral(
            "pulsesrc device=%1 "
            "do-timestamp=true "
            "! queue "
            "max-size-time=200000000 "
            "leaky=downstream "
            "! audioconvert "
            "! audioresample "
            "! audio/x-raw,"
            "rate=48000,"
            "channels=2 "
            "! opusenc "
            "bitrate=128000 "
            "frame-size=20 "
            "! rtpopuspay "
            "pt=96 "
            "! appsink "
            "name=desktop-audio-packet-sink "
            "emit-signals=true "
            "sync=false "
            "async=false "
            "max-buffers=8 "
            "drop=true")
            .arg(
                quotePipelineValue(
                    monitor));

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
                "Could not create desktop audio "
                "sender: ") +
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
            "desktop-audio-packet-sink");

    if (packetSink == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Desktop audio packet sink "
                "was not created."));

        stopSender();
        return false;
    }

    GstAppSinkCallbacks callbacks = {};

    callbacks.new_sample =
        onDesktopAudioPacket;

    gst_app_sink_set_callbacks(
        GST_APP_SINK(packetSink),
        &callbacks,
        this,
        nullptr);

    gst_object_unref(
        packetSink);

    if (
        !startPipeline(
            senderPipeline_,
            QStringLiteral(
                "Desktop audio sender"))
    ) {
        stopSender();
        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Desktop audio capture started."));

    return true;
}

bool RemoteDesktopAudio::startReceiver(
    const QString &outputNode)
{
    if (
        QThread::currentThread() !=
        thread()
    ) {
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
                    startReceiver(
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

    QString sink =
        QStringLiteral(
            "autoaudiosink");

    if (!outputNode.trimmed().isEmpty()) {
        sink =
            QStringLiteral(
                "pulsesink device=%1")
                .arg(
                    quotePipelineValue(
                        outputNode.trimmed()));
    }

    const QString pipelineDescription =
        QStringLiteral(
            "appsrc "
            "name=desktop-audio-packet-source "
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
            "! rtpopusdepay "
            "! opusdec "
            "! audioconvert "
            "! audioresample "
            "! %1")
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
                "Could not create desktop audio "
                "receiver: ") +
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
            "desktop-audio-packet-source");

    if (receiverAppSource_ == nullptr) {
        emit errorOccurred(
            QStringLiteral(
                "Desktop audio packet source "
                "was not created."));

        stopReceiver();
        return false;
    }

    if (
        !startPipeline(
            receiverPipeline_,
            QStringLiteral(
                "Desktop audio receiver"))
    ) {
        stopReceiver();
        return false;
    }

    emit statusChanged(
        QStringLiteral(
            "Desktop audio playback started."));

    return true;
}

void RemoteDesktopAudio::pushAudioPacket(
    const QByteArray &packet)
{
    if (
        receiverAppSource_ == nullptr ||
        packet.isEmpty() ||
        packet.size() > 64 * 1024
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

    GstFlowReturn result;

    g_signal_emit_by_name(
        receiverAppSource_,
        "push-buffer",
        buffer,
        &result);

    gst_buffer_unref(
        buffer);
}

void RemoteDesktopAudio::stopSender()
{
    if (
        QThread::currentThread() !=
        thread()
    ) {
        QMetaObject::invokeMethod(
            this,
            &RemoteDesktopAudio::stopSender,
            Qt::BlockingQueuedConnection);

        return;
    }

    stopPipeline(
        senderPipeline_);
}

void RemoteDesktopAudio::stopReceiver()
{
    if (
        QThread::currentThread() !=
        thread()
    ) {
        QMetaObject::invokeMethod(
            this,
            &RemoteDesktopAudio::stopReceiver,
            Qt::BlockingQueuedConnection);

        return;
    }

    if (receiverAppSource_ != nullptr) {
        gst_object_unref(
            receiverAppSource_);

        receiverAppSource_ = nullptr;
    }

    stopPipeline(
        receiverPipeline_);
}

void RemoteDesktopAudio::stop()
{
    stopSender();
    stopReceiver();
}

bool RemoteDesktopAudio::isSenderRunning() const
{
    return
        senderPipeline_ != nullptr;
}

bool RemoteDesktopAudio::isReceiverRunning() const
{
    return
        receiverPipeline_ != nullptr;
}
