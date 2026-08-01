#include "customer_voice_audio.h"

CustomerVoiceAudio::CustomerVoiceAudio(
    QObject *parent)
    : QObject(parent)
{
}

CustomerVoiceAudio::~CustomerVoiceAudio()
{
    stop();
}

bool CustomerVoiceAudio::startCustomerSender(
    const QString &,
    std::uint16_t,
    const QString &)
{
    emit errorOccurred(
        QStringLiteral(
            "Windows microphone transmission is not "
            "implemented yet."));

    return false;
}

bool CustomerVoiceAudio::startProviderReceiver(
    std::uint16_t,
    const QString &)
{
    emit errorOccurred(
        QStringLiteral(
            "Windows voice playback is not "
            "implemented yet."));

    return false;
}

void CustomerVoiceAudio::stopSender()
{
}

void CustomerVoiceAudio::stopReceiver()
{
}

void CustomerVoiceAudio::stop()
{
}

void CustomerVoiceAudio::setMuted(
    bool muted)
{
    muted_ = muted;
}

bool CustomerVoiceAudio::isRunning() const
{
    return false;
}

bool CustomerVoiceAudio::isMuted() const
{
    return muted_;
}

bool CustomerVoiceAudio::initializeGStreamer()
{
    return false;
}

QString CustomerVoiceAudio::quotePipelineValue(
    const QString &value)
{
    return value;
}

bool CustomerVoiceAudio::startPipeline(
    GstElement *,
    const QString &)
{
    return false;
}

void CustomerVoiceAudio::stopPipeline(
    GstElement *&pipeline)
{
    pipeline = nullptr;
}
