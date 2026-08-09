#pragma once

#include <array>
#include <cstdint>

namespace webrtc
{
class AudioProcessing;
}

class AssistAudioProcessor
{
public:
    static constexpr int sampleRate = 48000;
    static constexpr int channels = 1;
    static constexpr int samplesPerBlock = 480;

    AssistAudioProcessor();
    ~AssistAudioProcessor();

    AssistAudioProcessor(
        const AssistAudioProcessor &) = delete;

    AssistAudioProcessor &operator=(
        const AssistAudioProcessor &) = delete;

    bool initialize();

    void reset();

    void setStreamDelayMs(
        int delayMs);

    int streamDelayMs() const;

    bool processRender(
        const int16_t *input,
        int16_t *output);

    bool processCapture(
        const int16_t *input,
        int16_t *output);

    bool isInitialized() const;

private:
    webrtc::AudioProcessing *apm_ = nullptr;

    int streamDelayMs_ = 50;
};
