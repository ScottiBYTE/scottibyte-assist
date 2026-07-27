#include "pipewire_preview.h"

#include <QMetaObject>

#include <spa/buffer/buffer.h>
#include <spa/param/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace
{
constexpr auto previewName =
    "ScottiBYTE Assist Local Preview";

constexpr auto minimumFrameInterval =
    std::chrono::milliseconds(16);
}

PipeWirePreview::PipeWirePreview(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);

    streamEvents_.version =
        PW_VERSION_STREAM_EVENTS;

    streamEvents_.state_changed =
        &PipeWirePreview::handleStateChanged;

    streamEvents_.param_changed =
        &PipeWirePreview::handleParamChanged;

    streamEvents_.process =
        &PipeWirePreview::handleProcess;
}

PipeWirePreview::~PipeWirePreview()
{
    stop();
}

bool PipeWirePreview::isRunning() const
{
    return running_.load();
}

void PipeWirePreview::start(
    int portalFileDescriptor,
    uint nodeId)
{
    stop();

    if (portalFileDescriptor < 0) {
        emit errorOccurred(
            QStringLiteral(
                "The PipeWire portal file descriptor "
                "is invalid."));
        return;
    }

    if (nodeId == 0) {
        emit errorOccurred(
            QStringLiteral(
                "The PipeWire stream node ID is invalid."));
        return;
    }

    const int duplicatedFd =
        ::dup(portalFileDescriptor);

    if (duplicatedFd < 0) {
        emit errorOccurred(
            QStringLiteral(
                "Could not duplicate the PipeWire file "
                "descriptor: %1")
                .arg(
                    QString::fromLocal8Bit(
                        std::strerror(errno))));
        return;
    }

    threadLoop_ =
        pw_thread_loop_new(
            "scottibyte-assist-preview",
            nullptr);

    if (threadLoop_ == nullptr) {
        ::close(duplicatedFd);

        emit errorOccurred(
            QStringLiteral(
                "Could not create the PipeWire thread loop."));
        return;
    }

    context_ =
        pw_context_new(
            pw_thread_loop_get_loop(threadLoop_),
            nullptr,
            0);

    if (context_ == nullptr) {
        ::close(duplicatedFd);
        stop();

        emit errorOccurred(
            QStringLiteral(
                "Could not create the PipeWire context."));
        return;
    }

    core_ =
        pw_context_connect_fd(
            context_,
            duplicatedFd,
            nullptr,
            0);

    if (core_ == nullptr) {
        /*
         * PipeWire did not take ownership when connection
         * creation failed.
         */
        ::close(duplicatedFd);
        stop();

        emit errorOccurred(
            QStringLiteral(
                "Could not connect to the portal-provided "
                "PipeWire remote."));
        return;
    }

    stream_ =
        pw_stream_new(
            core_,
            previewName,
            pw_properties_new(
                PW_KEY_MEDIA_TYPE,
                "Video",
                PW_KEY_MEDIA_CATEGORY,
                "Capture",
                PW_KEY_MEDIA_ROLE,
                "Screen",
                nullptr));

    if (stream_ == nullptr) {
        stop();

        emit errorOccurred(
            QStringLiteral(
                "Could not create the PipeWire stream."));
        return;
    }

    pw_stream_add_listener(
        stream_,
        &streamListener_,
        &streamEvents_,
        this);

    std::array<std::byte, 1024> parameterBuffer{};

    spa_pod_builder builder =
        SPA_POD_BUILDER_INIT(
            parameterBuffer.data(),
            parameterBuffer.size());

    const spa_pod *parameters[] = {
        static_cast<const spa_pod *>(
            spa_pod_builder_add_object(
                &builder,
                SPA_TYPE_OBJECT_Format,
                SPA_PARAM_EnumFormat,

                SPA_FORMAT_mediaType,
                SPA_POD_Id(SPA_MEDIA_TYPE_video),

                SPA_FORMAT_mediaSubtype,
                SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),

                SPA_FORMAT_VIDEO_format,
                SPA_POD_CHOICE_ENUM_Id(
                    6,
                    SPA_VIDEO_FORMAT_BGRx,
                    SPA_VIDEO_FORMAT_BGRx,
                    SPA_VIDEO_FORMAT_BGRA,
                    SPA_VIDEO_FORMAT_RGBx,
                    SPA_VIDEO_FORMAT_RGBA,
                    SPA_VIDEO_FORMAT_RGB,
                    SPA_VIDEO_FORMAT_BGR)))
    };

    const int connectResult =
        pw_stream_connect(
            stream_,
            PW_DIRECTION_INPUT,
            nodeId,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT |
                PW_STREAM_FLAG_MAP_BUFFERS),
            parameters,
            1);

    if (connectResult < 0) {
        const QString message =
            QStringLiteral(
                "Could not connect to PipeWire node %1: %2")
                .arg(nodeId)
                .arg(
                    QString::fromLocal8Bit(
                        spa_strerror(connectResult)));

        stop();
        emit errorOccurred(message);
        return;
    }

    const int startResult =
        pw_thread_loop_start(threadLoop_);

    if (startResult < 0) {
        const QString message =
            QStringLiteral(
                "Could not start the PipeWire thread: %1")
                .arg(
                    QString::fromLocal8Bit(
                        spa_strerror(startResult)));

        stop();
        emit errorOccurred(message);
        return;
    }

    running_.store(true);
    previousFrameTime_ = {};

    emit statusChanged(
        QStringLiteral(
            "Connected to PipeWire node %1; "
            "waiting for video format…")
            .arg(nodeId));
}

void PipeWirePreview::stop()
{
    running_.store(false);
    framePending_.store(false);

    if (threadLoop_ != nullptr) {
        pw_thread_loop_stop(threadLoop_);
    }

    if (stream_ != nullptr) {
        spa_hook_remove(&streamListener_);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }

    if (core_ != nullptr) {
        pw_core_disconnect(core_);
        core_ = nullptr;
    }

    if (context_ != nullptr) {
        pw_context_destroy(context_);
        context_ = nullptr;
    }

    if (threadLoop_ != nullptr) {
        pw_thread_loop_destroy(threadLoop_);
        threadLoop_ = nullptr;
    }

    videoInfo_ = {};
}

void PipeWirePreview::acknowledgeFrame()
{
    framePending_.store(false);
}

void PipeWirePreview::handleStateChanged(
    void *data,
    enum pw_stream_state,
    enum pw_stream_state newState,
    const char *error)
{
    auto *preview =
        static_cast<PipeWirePreview *>(data);

    if (newState == PW_STREAM_STATE_ERROR) {
        const QString message =
            QStringLiteral("PipeWire stream error: %1")
                .arg(
                    error != nullptr
                        ? QString::fromLocal8Bit(error)
                        : QStringLiteral("unknown error"));

        QMetaObject::invokeMethod(
            preview,
            [preview, message]()
            {
                emit preview->errorOccurred(message);
            },
            Qt::QueuedConnection);

        return;
    }

    if (newState == PW_STREAM_STATE_STREAMING) {
        QMetaObject::invokeMethod(
            preview,
            [preview]()
            {
                emit preview->statusChanged(
                    QStringLiteral(
                        "PipeWire desktop stream is active"));
            },
            Qt::QueuedConnection);
    }
}

void PipeWirePreview::handleParamChanged(
    void *data,
    uint32_t id,
    const struct spa_pod *parameter)
{
    auto *preview =
        static_cast<PipeWirePreview *>(data);

    if (parameter == nullptr ||
        id != SPA_PARAM_Format) {
        return;
    }

    spa_video_info_raw parsedInfo{};

    const int parseResult =
        spa_format_video_raw_parse(
            parameter,
            &parsedInfo);

    if (parseResult < 0) {
        QMetaObject::invokeMethod(
            preview,
            [preview]()
            {
                emit preview->errorOccurred(
                    QStringLiteral(
                        "PipeWire supplied an unsupported "
                        "video format."));
            },
            Qt::QueuedConnection);

        return;
    }

    preview->videoInfo_ = parsedInfo;

    const QString description =
        QStringLiteral(
            "PipeWire format negotiated: %1 × %2 at %3/%4")
            .arg(parsedInfo.size.width)
            .arg(parsedInfo.size.height)
            .arg(parsedInfo.framerate.num)
            .arg(parsedInfo.framerate.denom);

    QMetaObject::invokeMethod(
        preview,
        [preview, description]()
        {
            emit preview->statusChanged(description);
        },
        Qt::QueuedConnection);
}

void PipeWirePreview::handleProcess(void *data)
{
    auto *preview =
        static_cast<PipeWirePreview *>(data);

    preview->processFrame();
}

void PipeWirePreview::processFrame()
{
    if (!running_.load() ||
        stream_ == nullptr) {
        return;
    }

    pw_buffer *pipeWireBuffer =
        pw_stream_dequeue_buffer(stream_);

    if (pipeWireBuffer == nullptr) {
        return;
    }

    spa_buffer *buffer =
        pipeWireBuffer->buffer;

    if (buffer == nullptr ||
        buffer->n_datas == 0) {
        pw_stream_queue_buffer(
            stream_,
            pipeWireBuffer);
        return;
    }

    spa_data &data = buffer->datas[0];

    if (data.data == nullptr ||
        data.chunk == nullptr ||
        data.chunk->size == 0) {
        pw_stream_queue_buffer(
            stream_,
            pipeWireBuffer);
        return;
    }

    const uint width =
        videoInfo_.size.width;

    const uint height =
        videoInfo_.size.height;

    const QImage::Format imageFormat =
        imageFormatFor(videoInfo_.format);

    if (width == 0 ||
        height == 0 ||
        imageFormat == QImage::Format_Invalid) {
        pw_stream_queue_buffer(
            stream_,
            pipeWireBuffer);
        return;
    }

    const auto now =
        std::chrono::steady_clock::now();

    if (previousFrameTime_ !=
            std::chrono::steady_clock::time_point{} &&
        now - previousFrameTime_ <
            minimumFrameInterval) {
        pw_stream_queue_buffer(
            stream_,
            pipeWireBuffer);
        return;
    }

    if (framePending_.exchange(true)) {
        pw_stream_queue_buffer(
            stream_,
            pipeWireBuffer);
        return;
    }

    previousFrameTime_ = now;

    const int minimumStride =
        static_cast<int>(width) *
        ((imageFormat == QImage::Format_RGB888 ||
          imageFormat == QImage::Format_BGR888)
             ? 3
             : 4);

    const int stride =
        data.chunk->stride != 0
            ? std::abs(data.chunk->stride)
            : minimumStride;

    const auto *bytes =
        static_cast<const uchar *>(data.data) +
        data.chunk->offset;

    const QImage borrowedImage(
        bytes,
        static_cast<int>(width),
        static_cast<int>(height),
        stride,
        imageFormat);

    const QImage copiedImage =
        borrowedImage.copy();

    pw_stream_queue_buffer(
        stream_,
        pipeWireBuffer);

    if (copiedImage.isNull()) {
        framePending_.store(false);
        return;
    }

    emit frameReady(copiedImage);
}

QImage::Format PipeWirePreview::imageFormatFor(
    enum spa_video_format format) const
{
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRx:
        return QImage::Format_RGB32;

    case SPA_VIDEO_FORMAT_BGRA:
        return QImage::Format_ARGB32;

    case SPA_VIDEO_FORMAT_RGBx:
    case SPA_VIDEO_FORMAT_RGBA:
        return QImage::Format_RGBA8888;

    case SPA_VIDEO_FORMAT_RGB:
        return QImage::Format_RGB888;

    case SPA_VIDEO_FORMAT_BGR:
        return QImage::Format_BGR888;

    default:
        return QImage::Format_Invalid;
    }
}
