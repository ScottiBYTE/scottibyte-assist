#include "vp8_video_codec.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <QMutex>
#include <QMutexLocker>

namespace
{

QMutex gstreamerInitializationMutex;
bool gstreamerInitializationAttempted = false;
bool gstreamerInitializationSucceeded = false;

QString gstErrorMessage(
    const QString &prefix,
    GError *error)
{
    if (error == nullptr) {
        return prefix;
    }

    const QString detail =
        QString::fromUtf8(
            error->message == nullptr
                ? "Unknown GStreamer error."
                : error->message);

    return prefix + QStringLiteral(": ") + detail;
}

}

Vp8VideoCodec::Vp8VideoCodec(
    QObject *parent)
    : QObject(parent)
{
    initializeGStreamer();
}

Vp8VideoCodec::~Vp8VideoCodec()
{
    destroyEncoder();
    destroyDecoder();
}

bool Vp8VideoCodec::initializeGStreamer()
{
    QMutexLocker locker(
        &gstreamerInitializationMutex);

    if (gstreamerInitializationAttempted) {
        return gstreamerInitializationSucceeded;
    }

    gstreamerInitializationAttempted = true;

    GError *error = nullptr;

    gstreamerInitializationSucceeded =
        gst_init_check(
            nullptr,
            nullptr,
            &error);

    if (error != nullptr) {
        g_error_free(error);
    }

    return gstreamerInitializationSucceeded;
}

void Vp8VideoCodec::setError(
    const QString &message)
{
    lastError_ = message;
}

QString Vp8VideoCodec::lastError() const
{
    return lastError_;
}

bool Vp8VideoCodec::ensureEncoder(
    int width,
    int height)
{
    if (
        encoderPipeline_ != nullptr &&
        encoderWidth_ == width &&
        encoderHeight_ == height
    ) {
        return true;
    }

    destroyEncoder();

    if (!initializeGStreamer()) {
        setError(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    const QString pipelineDescription =
        QStringLiteral(
            "appsrc name=assist_source "
            "is-live=true "
            "format=time "
            "do-timestamp=false "
            "block=true "
            "caps=video/x-raw,"
            "format=RGBA,"
            "width=%1,"
            "height=%2,"
            "framerate=30/1 "
            "! queue max-size-buffers=1 leaky=downstream "
            "! videoconvert "
            "! vp8enc "
            "deadline=1 "
            "cpu-used=8 "
            "threads=4 "
            "target-bitrate=6000000 "
            "keyframe-max-dist=30 "
            "error-resilient=partitions "
            "! appsink name=assist_sink "
            "emit-signals=false "
            "sync=false "
            "max-buffers=1 "
            "drop=true")
            .arg(width)
            .arg(height);

    GError *error = nullptr;

    encoderPipeline_ =
        gst_parse_launch(
            pipelineDescription
                .toUtf8()
                .constData(),
            &error);

    if (encoderPipeline_ == nullptr) {
        setError(
            gstErrorMessage(
                QStringLiteral(
                    "Unable to create VP8 encoder"),
                error));

        if (error != nullptr) {
            g_error_free(error);
        }

        return false;
    }

    if (error != nullptr) {
        g_error_free(error);
    }

    encoderSource_ =
        gst_bin_get_by_name(
            GST_BIN(encoderPipeline_),
            "assist_source");

    encoderSink_ =
        gst_bin_get_by_name(
            GST_BIN(encoderPipeline_),
            "assist_sink");

    if (
        encoderSource_ == nullptr ||
        encoderSink_ == nullptr
    ) {
        setError(
            QStringLiteral(
                "The VP8 encoder pipeline is incomplete."));
        destroyEncoder();
        return false;
    }

    const GstStateChangeReturn stateResult =
        gst_element_set_state(
            encoderPipeline_,
            GST_STATE_PLAYING);

    if (
        stateResult ==
        GST_STATE_CHANGE_FAILURE
    ) {
        setError(
            QStringLiteral(
                "The VP8 encoder could not start."));
        destroyEncoder();
        return false;
    }

    encoderWidth_ = width;
    encoderHeight_ = height;
    encoderFrameNumber_ = 0;

    return true;
}

bool Vp8VideoCodec::ensureDecoder()
{
    if (decoderPipeline_ != nullptr) {
        return true;
    }

    if (!initializeGStreamer()) {
        setError(
            QStringLiteral(
                "GStreamer could not be initialized."));
        return false;
    }

    const char *pipelineDescription =
        "appsrc name=assist_source "
        "is-live=true "
        "format=time "
        "block=true "
        "caps=video/x-vp8 "
        "! queue max-size-buffers=2 leaky=downstream "
        "! vp8dec "
        "! videoconvert "
        "! video/x-raw,format=RGBA "
        "! appsink name=assist_sink "
        "emit-signals=false "
        "sync=false "
        "max-buffers=1 "
        "drop=true";

    GError *error = nullptr;

    decoderPipeline_ =
        gst_parse_launch(
            pipelineDescription,
            &error);

    if (decoderPipeline_ == nullptr) {
        setError(
            gstErrorMessage(
                QStringLiteral(
                    "Unable to create VP8 decoder"),
                error));

        if (error != nullptr) {
            g_error_free(error);
        }

        return false;
    }

    if (error != nullptr) {
        g_error_free(error);
    }

    decoderSource_ =
        gst_bin_get_by_name(
            GST_BIN(decoderPipeline_),
            "assist_source");

    decoderSink_ =
        gst_bin_get_by_name(
            GST_BIN(decoderPipeline_),
            "assist_sink");

    if (
        decoderSource_ == nullptr ||
        decoderSink_ == nullptr
    ) {
        setError(
            QStringLiteral(
                "The VP8 decoder pipeline is incomplete."));
        destroyDecoder();
        return false;
    }

    const GstStateChangeReturn stateResult =
        gst_element_set_state(
            decoderPipeline_,
            GST_STATE_PLAYING);

    if (
        stateResult ==
        GST_STATE_CHANGE_FAILURE
    ) {
        setError(
            QStringLiteral(
                "The VP8 decoder could not start."));
        destroyDecoder();
        return false;
    }

    return true;
}

bool Vp8VideoCodec::encodeFrame(
    const QImage &frame,
    QByteArray &encoded)
{
    encoded.clear();
    lastError_.clear();

    if (frame.isNull()) {
        setError(
            QStringLiteral(
                "The source frame is empty."));
        return false;
    }

    const QImage rgba =
        frame.convertToFormat(
            QImage::Format_RGBA8888);

    if (
        !ensureEncoder(
            rgba.width(),
            rgba.height())
    ) {
        return false;
    }

    const qsizetype byteCount =
        rgba.sizeInBytes();

    GstBuffer *buffer =
        gst_buffer_new_allocate(
            nullptr,
            static_cast<gsize>(
                byteCount),
            nullptr);

    if (buffer == nullptr) {
        setError(
            QStringLiteral(
                "Unable to allocate the VP8 input buffer."));
        return false;
    }

    gst_buffer_fill(
        buffer,
        0,
        rgba.constBits(),
        static_cast<gsize>(
            byteCount));

    constexpr GstClockTime frameDuration =
        GST_SECOND / 30;

    GST_BUFFER_PTS(buffer) =
        encoderFrameNumber_ *
        frameDuration;

    GST_BUFFER_DTS(buffer) =
        GST_CLOCK_TIME_NONE;

    GST_BUFFER_DURATION(buffer) =
        frameDuration;

    ++encoderFrameNumber_;

    const GstFlowReturn pushResult =
        gst_app_src_push_buffer(
            GST_APP_SRC(
                encoderSource_),
            buffer);

    if (pushResult != GST_FLOW_OK) {
        setError(
            QStringLiteral(
                "The VP8 encoder rejected the frame."));
        return false;
    }

    GstSample *sample =
        gst_app_sink_try_pull_sample(
            GST_APP_SINK(
                encoderSink_),
            500 * GST_MSECOND);

    if (sample == nullptr) {
        setError(
            QStringLiteral(
                "The VP8 encoder did not return a frame."));
        return false;
    }

    GstBuffer *encodedBuffer =
        gst_sample_get_buffer(sample);

    GstMapInfo map{};

    if (
        encodedBuffer == nullptr ||
        !gst_buffer_map(
            encodedBuffer,
            &map,
            GST_MAP_READ)
    ) {
        gst_sample_unref(sample);

        setError(
            QStringLiteral(
                "Unable to read the encoded VP8 frame."));
        return false;
    }

    encoded =
        QByteArray(
            reinterpret_cast<
                const char *>(map.data),
            static_cast<qsizetype>(
                map.size));

    gst_buffer_unmap(
        encodedBuffer,
        &map);

    gst_sample_unref(sample);

    if (encoded.isEmpty()) {
        setError(
            QStringLiteral(
                "The encoded VP8 frame is empty."));
        return false;
    }

    return true;
}

bool Vp8VideoCodec::decodeFrame(
    const QByteArray &encoded,
    QImage &frame)
{
    frame = QImage();
    lastError_.clear();

    if (encoded.isEmpty()) {
        setError(
            QStringLiteral(
                "The encoded VP8 frame is empty."));
        return false;
    }

    if (!ensureDecoder()) {
        return false;
    }

    GstBuffer *buffer =
        gst_buffer_new_allocate(
            nullptr,
            static_cast<gsize>(
                encoded.size()),
            nullptr);

    if (buffer == nullptr) {
        setError(
            QStringLiteral(
                "Unable to allocate the VP8 decoder buffer."));
        return false;
    }

    gst_buffer_fill(
        buffer,
        0,
        encoded.constData(),
        static_cast<gsize>(
            encoded.size()));

    const GstFlowReturn pushResult =
        gst_app_src_push_buffer(
            GST_APP_SRC(
                decoderSource_),
            buffer);

    if (pushResult != GST_FLOW_OK) {
        setError(
            QStringLiteral(
                "The VP8 decoder rejected the frame."));
        return false;
    }

    GstSample *sample =
        gst_app_sink_try_pull_sample(
            GST_APP_SINK(
                decoderSink_),
            500 * GST_MSECOND);

    if (sample == nullptr) {
        setError(
            QStringLiteral(
                "The VP8 decoder did not return an image."));
        return false;
    }

    GstCaps *caps =
        gst_sample_get_caps(sample);

    GstBuffer *decodedBuffer =
        gst_sample_get_buffer(sample);

    if (
        caps == nullptr ||
        decodedBuffer == nullptr
    ) {
        gst_sample_unref(sample);

        setError(
            QStringLiteral(
                "The decoded VP8 sample is incomplete."));
        return false;
    }

    const GstStructure *structure =
        gst_caps_get_structure(
            caps,
            0);

    int width = 0;
    int height = 0;

    if (
        structure == nullptr ||
        !gst_structure_get_int(
            structure,
            "width",
            &width) ||
        !gst_structure_get_int(
            structure,
            "height",
            &height) ||
        width <= 0 ||
        height <= 0
    ) {
        gst_sample_unref(sample);

        setError(
            QStringLiteral(
                "The decoded VP8 dimensions are invalid."));
        return false;
    }

    GstMapInfo map{};

    if (
        !gst_buffer_map(
            decodedBuffer,
            &map,
            GST_MAP_READ)
    ) {
        gst_sample_unref(sample);

        setError(
            QStringLiteral(
                "Unable to read the decoded VP8 image."));
        return false;
    }

    const qsizetype minimumBytes =
        static_cast<qsizetype>(width) *
        static_cast<qsizetype>(height) *
        4;

    if (
        static_cast<qsizetype>(
            map.size) <
        minimumBytes
    ) {
        gst_buffer_unmap(
            decodedBuffer,
            &map);

        gst_sample_unref(sample);

        setError(
            QStringLiteral(
                "The decoded VP8 image buffer is too small."));
        return false;
    }

    const QImage wrapped(
        map.data,
        width,
        height,
        width * 4,
        QImage::Format_RGBA8888);

    frame = wrapped.copy();

    gst_buffer_unmap(
        decodedBuffer,
        &map);

    gst_sample_unref(sample);

    return !frame.isNull();
}

void Vp8VideoCodec::destroyEncoder()
{
    if (encoderPipeline_ != nullptr) {
        gst_element_set_state(
            encoderPipeline_,
            GST_STATE_NULL);
    }

    if (encoderSource_ != nullptr) {
        gst_object_unref(
            encoderSource_);
        encoderSource_ = nullptr;
    }

    if (encoderSink_ != nullptr) {
        gst_object_unref(
            encoderSink_);
        encoderSink_ = nullptr;
    }

    if (encoderPipeline_ != nullptr) {
        gst_object_unref(
            encoderPipeline_);
        encoderPipeline_ = nullptr;
    }

    encoderWidth_ = 0;
    encoderHeight_ = 0;
    encoderFrameNumber_ = 0;
}

void Vp8VideoCodec::destroyDecoder()
{
    if (decoderPipeline_ != nullptr) {
        gst_element_set_state(
            decoderPipeline_,
            GST_STATE_NULL);
    }

    if (decoderSource_ != nullptr) {
        gst_object_unref(
            decoderSource_);
        decoderSource_ = nullptr;
    }

    if (decoderSink_ != nullptr) {
        gst_object_unref(
            decoderSink_);
        decoderSink_ = nullptr;
    }

    if (decoderPipeline_ != nullptr) {
        gst_object_unref(
            decoderPipeline_);
        decoderPipeline_ = nullptr;
    }
}
