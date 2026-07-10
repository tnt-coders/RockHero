#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <filesystem>
#include <fstream>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <rock_hero/common/audio/song/audio_transcode.h>
#include <rock_hero/common/audio/testing/audio_fixtures.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <system_error>

namespace rock_hero::common::audio
{

namespace
{

// Writes a real WAV sine tone (built by JUCE's writer) so the transcoder exercises an actual
// decode -> re-encode round-trip rather than a stub.
[[nodiscard]] std::filesystem::path writeSineWav(
    const std::filesystem::path& path, double sample_rate, int channels, int total_samples)
{
    const std::string bytes = testing::makeWavBytes(sample_rate, channels, total_samples);
    std::ofstream stream{path, std::ios::binary};
    stream << bytes;
    return path;
}

} // namespace

// Transcoding produces a smaller, losslessly readable FLAC that keeps the source's rate, channel
// count, and length, so playback and the waveform thumbnail later decode identical samples.
TEST_CASE(
    "transcodeToFlac writes a compact FLAC preserving the source format", "[audio][transcode]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_transcode_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    std::filesystem::create_directories(scratch);

    constexpr double sample_rate = 44100.0;
    constexpr int channels = 2;
    constexpr int total_samples = 8192;
    const std::filesystem::path wav =
        writeSineWav(scratch / "source.wav", sample_rate, channels, total_samples);
    const std::filesystem::path flac = scratch / "backing.flac";

    const auto result = transcodeToFlac(wav, flac);
    REQUIRE(result.has_value());
    REQUIRE(std::filesystem::is_regular_file(flac));
    // FLAC losslessly compresses the tone, so it is smaller than the source WAV.
    CHECK(std::filesystem::file_size(flac) < std::filesystem::file_size(wav));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(
        common::core::juceFileFromPath(flac))};
    REQUIRE(reader != nullptr);
    CHECK(std::is_eq(reader->sampleRate <=> sample_rate));
    CHECK(static_cast<int>(reader->numChannels) == channels);
    CHECK(reader->lengthInSamples == total_samples);
    // The 16-bit source stays 16-bit rather than being padded out to 24-bit FLAC.
    CHECK(reader->bitsPerSample == 16);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A source that no audio format can decode fails with the typed undecodable-source error rather
// than producing a broken FLAC.
TEST_CASE("transcodeToFlac reports an undecodable source", "[audio][transcode]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_transcode_bad_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    std::filesystem::create_directories(scratch);

    const std::filesystem::path junk = scratch / "not_audio.bin";
    {
        std::ofstream stream{junk, std::ios::binary};
        stream << "this is plainly not decodable audio data";
    }

    const std::filesystem::path flac = scratch / "backing.flac";
    const auto result = transcodeToFlac(junk, flac);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioTranscodeErrorCode::SourceUndecodable);
    CHECK_FALSE(std::filesystem::exists(flac));

    std::filesystem::remove_all(scratch, cleanup_error);
}

} // namespace rock_hero::common::audio
