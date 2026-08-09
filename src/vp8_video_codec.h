#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

typedef struct _GstElement GstElement;

class Vp8VideoCodec : public QObject
{
    Q_OBJECT

public:
    explicit Vp8VideoCodec(
        QObject *parent = nullptr);

    ~Vp8VideoCodec() override;

    bool encodeFrame(
        const QImage &frame,
        QByteArray &encoded);

    bool decodeFrame(
        const QByteArray &encoded,
        QImage &frame);

    QString lastError() const;

private:
    static bool initializeGStreamer();

    bool ensureEncoder(
        int width,
        int height);

    bool ensureDecoder();

    void destroyEncoder();
    void destroyDecoder();

    void setError(
        const QString &message);

    GstElement *encoderPipeline_ = nullptr;
    GstElement *encoderSource_ = nullptr;
    GstElement *encoderSink_ = nullptr;

    GstElement *decoderPipeline_ = nullptr;
    GstElement *decoderSource_ = nullptr;
    GstElement *decoderSink_ = nullptr;

    int encoderWidth_ = 0;
    int encoderHeight_ = 0;

    quint64 encoderFrameNumber_ = 0;

    QString lastError_;
};
