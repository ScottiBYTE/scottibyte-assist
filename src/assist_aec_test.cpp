#include "assist_audio_processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{

constexpr int delayMs = 50;

constexpr int delaySamples =
    AssistAudioProcessor::sampleRate *
    delayMs /
    1000;

double rms(
    const std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        &samples)
{
    double sum = 0.0;

    for (const int16_t sample : samples) {
        const double value =
            static_cast<double>(sample);

        sum += value * value;
    }

    return std::sqrt(
        sum /
        static_cast<double>(
            samples.size()));
}

double toneMagnitude(
    const std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        &samples,
    double frequency,
    std::int64_t blockStartSample)
{
    double sinSum = 0.0;
    double cosSum = 0.0;

    for (int i = 0;
         i <
             AssistAudioProcessor::
                 samplesPerBlock;
         ++i) {

        const std::int64_t absoluteSample =
            blockStartSample + i;

        const double phase =
            2.0 *
            3.14159265358979323846 *
            frequency *
            static_cast<double>(
                absoluteSample) /
            AssistAudioProcessor::sampleRate;

        const double value =
            static_cast<double>(
                samples[i]);

        sinSum +=
            value *
            std::sin(phase);

        cosSum +=
            value *
            std::cos(phase);
    }

    return
        2.0 *
        std::sqrt(
            sinSum * sinSum +
            cosSum * cosSum) /
        AssistAudioProcessor::
            samplesPerBlock;
}

int16_t clampSample(
    double value)
{
    value = std::clamp(
        value,
        -32768.0,
        32767.0);

    return static_cast<int16_t>(
        std::lround(value));
}

double changeDb(
    double before,
    double after)
{
    if (
        before <= 0.0 ||
        after <= 0.0
    ) {
        return 0.0;
    }

    return
        20.0 *
        std::log10(
            after /
            before);
}

}

int main()
{
    AssistAudioProcessor processor;

    if (!processor.initialize()) {
        std::cerr
            << "FAIL: AssistAudioProcessor initialization\n";

        return 1;
    }

    processor.setStreamDelayMs(
        delayMs);

    std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        render{};

    std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        renderProcessed{};

    std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        capture{};

    std::array<
        int16_t,
        AssistAudioProcessor::samplesPerBlock>
        captureProcessed{};

    std::array<int16_t, delaySamples>
        renderHistory{};

    std::size_t historyPosition = 0;

    double inputRmsTotal = 0.0;
    double outputRmsTotal = 0.0;

    double inputEchoTotal = 0.0;
    double outputEchoTotal = 0.0;

    double inputNearTotal = 0.0;
    double outputNearTotal = 0.0;

    constexpr int totalBlocks = 500;
    constexpr int measurementStartBlock = 200;

    for (int block = 0;
         block < totalBlocks;
         ++block) {

        for (int i = 0;
             i <
                 AssistAudioProcessor::
                     samplesPerBlock;
             ++i) {

            const std::int64_t absoluteSample =
                static_cast<std::int64_t>(block) *
                    AssistAudioProcessor::
                        samplesPerBlock +
                i;

            const double time =
                static_cast<double>(
                    absoluteSample) /
                AssistAudioProcessor::sampleRate;

            const double farEnd =
                9000.0 *
                std::sin(
                    2.0 *
                    3.14159265358979323846 *
                    440.0 *
                    time);

            render[i] =
                clampSample(farEnd);

            const int16_t delayed =
                renderHistory[
                    historyPosition];

            renderHistory[
                historyPosition] =
                    render[i];

            historyPosition =
                (
                    historyPosition + 1
                ) %
                renderHistory.size();

            const double nearEnd =
                2500.0 *
                std::sin(
                    2.0 *
                    3.14159265358979323846 *
                    880.0 *
                    time);

            capture[i] =
                clampSample(
                    static_cast<double>(
                        delayed) *
                        0.55 +
                    nearEnd);
        }

        if (
            !processor.processRender(
                render.data(),
                renderProcessed.data())
        ) {
            std::cerr
                << "FAIL: render processing at block "
                << block
                << '\n';

            return 2;
        }

        if (
            !processor.processCapture(
                capture.data(),
                captureProcessed.data())
        ) {
            std::cerr
                << "FAIL: capture processing at block "
                << block
                << '\n';

            return 3;
        }

        if (
            block >=
            measurementStartBlock
        ) {
            inputRmsTotal +=
                rms(capture);

            outputRmsTotal +=
                rms(captureProcessed);

            const std::int64_t blockStartSample =
                static_cast<std::int64_t>(block) *
                AssistAudioProcessor::
                    samplesPerBlock;

            inputEchoTotal +=
                toneMagnitude(
                    capture,
                    440.0,
                    blockStartSample);

            outputEchoTotal +=
                toneMagnitude(
                    captureProcessed,
                    440.0,
                    blockStartSample);

            inputNearTotal +=
                toneMagnitude(
                    capture,
                    880.0,
                    blockStartSample);

            outputNearTotal +=
                toneMagnitude(
                    captureProcessed,
                    880.0,
                    blockStartSample);
        }
    }

    const int measuredBlocks =
        totalBlocks -
        measurementStartBlock;

    const double inputAverage =
        inputRmsTotal /
        measuredBlocks;

    const double outputAverage =
        outputRmsTotal /
        measuredBlocks;

    const double inputEchoAverage =
        inputEchoTotal /
        measuredBlocks;

    const double outputEchoAverage =
        outputEchoTotal /
        measuredBlocks;

    const double inputNearAverage =
        inputNearTotal /
        measuredBlocks;

    const double outputNearAverage =
        outputNearTotal /
        measuredBlocks;

    std::cout
        << "Assist AEC3 processor PASS\n"
        << "Sample rate: "
        << AssistAudioProcessor::sampleRate
        << " Hz\n"
        << "Block: "
        << AssistAudioProcessor::samplesPerBlock
        << " samples / 10 ms\n"
        << "Configured stream delay: "
        << processor.streamDelayMs()
        << " ms\n"
        << "Measured input RMS: "
        << inputAverage
        << '\n'
        << "Measured output RMS: "
        << outputAverage
        << '\n'
        << "440 Hz echo input magnitude: "
        << inputEchoAverage
        << '\n'
        << "440 Hz echo output magnitude: "
        << outputEchoAverage
        << '\n'
        << "440 Hz echo change: "
        << changeDb(
            inputEchoAverage,
            outputEchoAverage)
        << " dB\n"
        << "880 Hz near-end input magnitude: "
        << inputNearAverage
        << '\n'
        << "880 Hz near-end output magnitude: "
        << outputNearAverage
        << '\n'
        << "880 Hz near-end change: "
        << changeDb(
            inputNearAverage,
            outputNearAverage)
        << " dB\n";

    return 0;
}
