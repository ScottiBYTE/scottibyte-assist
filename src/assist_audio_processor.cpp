#include "assist_audio_processor.h"

#include <modules/audio_processing/include/audio_processing.h>

#include <algorithm>

AssistAudioProcessor::AssistAudioProcessor() = default;

AssistAudioProcessor::~AssistAudioProcessor()
{
    reset();
}

bool AssistAudioProcessor::initialize()
{
    reset();

    apm_ =
        webrtc::AudioProcessingBuilder().
            Create();

    if (apm_ == nullptr) {
        return false;
    }

    webrtc::AudioProcessing::Config config;

    config.pipeline.maximum_internal_processing_rate =
        sampleRate;

    config.echo_canceller.enabled = true;
    config.echo_canceller.mobile_mode = false;

    config.noise_suppression.enabled = true;
    config.noise_suppression.level =
        webrtc::AudioProcessing::Config::
            NoiseSuppression::kModerate;

    config.high_pass_filter.enabled = true;

    /*
     * Gain control remains disabled for the first
     * production AEC implementation. We want to
     * establish clean echo cancellation before adding
     * another adaptive element to the microphone path.
     */
    config.gain_controller1.enabled = false;
    config.gain_controller2.enabled = false;

    apm_->ApplyConfig(config);

    return true;
}

void AssistAudioProcessor::reset()
{
    if (apm_ == nullptr) {
        return;
    }

    apm_->Release();
    apm_ = nullptr;
}

void AssistAudioProcessor::setStreamDelayMs(
    int delayMs)
{
    streamDelayMs_ =
        std::clamp(
            delayMs,
            0,
            500);
}

int AssistAudioProcessor::streamDelayMs() const
{
    return streamDelayMs_;
}

bool AssistAudioProcessor::processRender(
    const int16_t *input,
    int16_t *output)
{
    if (
        apm_ == nullptr ||
        input == nullptr ||
        output == nullptr
    ) {
        return false;
    }

    const webrtc::StreamConfig streamConfig(
        sampleRate,
        channels,
        false);

    return
        apm_->ProcessReverseStream(
            input,
            streamConfig,
            streamConfig,
            output) == 0;
}

bool AssistAudioProcessor::processCapture(
    const int16_t *input,
    int16_t *output)
{
    if (
        apm_ == nullptr ||
        input == nullptr ||
        output == nullptr
    ) {
        return false;
    }

    if (
        apm_->set_stream_delay_ms(
            streamDelayMs_) != 0
    ) {
        return false;
    }

    const webrtc::StreamConfig streamConfig(
        sampleRate,
        channels,
        false);

    return
        apm_->ProcessStream(
            input,
            streamConfig,
            streamConfig,
            output) == 0;
}

bool AssistAudioProcessor::isInitialized() const
{
    return apm_ != nullptr;
}
