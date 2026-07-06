/*!
\file audio_fixtures.h
\brief Shared audio fixture builders for tests.
*/

#pragma once

#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <numbers>
#include <string>

namespace rock_hero::common::audio::testing
{

// Encodes a real 16-bit PCM WAV sine tone in memory through JUCE's own WAV writer and returns the
// raw bytes, so tests can embed it in an archive or write it to disk without hand-assembling a WAV
// header.
[[nodiscard]] inline std::string makeWavBytes(double sample_rate, int channels, int total_samples)
{
    juce::AudioBuffer<float> buffer{channels, total_samples};
    for (int sample = 0; sample < total_samples; ++sample)
    {
        const auto value = static_cast<float>(
            0.25 * std::sin(2.0 * std::numbers::pi * 440.0 * sample / sample_rate));
        for (int channel = 0; channel < channels; ++channel)
        {
            buffer.setSample(channel, sample, value);
        }
    }

    juce::MemoryBlock block;
    {
        auto memory_stream = std::make_unique<juce::MemoryOutputStream>(block, false);
        std::unique_ptr<juce::OutputStream> stream{std::move(memory_stream)};
        juce::WavAudioFormat wav_format;
        const std::unique_ptr<juce::AudioFormatWriter> writer{wav_format.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}
                .withSampleRate(sample_rate)
                .withNumChannels(channels)
                .withBitsPerSample(16))};
        if (writer != nullptr)
        {
            writer->writeFromAudioSampleBuffer(buffer, 0, total_samples);
        }
        // The writer flushes into the memory block as it is destroyed at the end of this scope.
    }
    return std::string{static_cast<const char*>(block.getData()), block.getSize()};
}

} // namespace rock_hero::common::audio::testing
