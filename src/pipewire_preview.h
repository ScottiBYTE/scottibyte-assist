#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>

#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>

class PipeWirePreview final : public QObject
{
    Q_OBJECT

public:
    explicit PipeWirePreview(QObject *parent = nullptr);
    ~PipeWirePreview() override;

    bool isRunning() const;

public slots:
    void start(int portalFileDescriptor, uint nodeId);
    void stop();
    void acknowledgeFrame();

signals:
    void frameReady(const QImage &frame);
    void statusChanged(const QString &status);
    void errorOccurred(const QString &message);

private:
    static void handleStateChanged(
        void *data,
        enum pw_stream_state oldState,
        enum pw_stream_state newState,
        const char *error);

    static void handleParamChanged(
        void *data,
        uint32_t id,
        const struct spa_pod *parameter);

    static void handleProcess(void *data);

    void processFrame();
    QImage::Format imageFormatFor(
        enum spa_video_format format) const;

    pw_thread_loop *threadLoop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_stream *stream_ = nullptr;

    spa_hook streamListener_{};
    pw_stream_events streamEvents_{};

    spa_video_info_raw videoInfo_{};

    std::atomic_bool framePending_{false};
    std::atomic_bool running_{false};

    std::chrono::steady_clock::time_point
        previousFrameTime_{};
};
