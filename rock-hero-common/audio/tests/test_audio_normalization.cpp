#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <numbers>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/domain/audio_normalization.h>
#include <rock_hero/common/core/infrastructure/juce_path.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Sample rate used by every generated WAV fixture. Matches typical backing audio so the analyzer
// sees the same WAV shape it will hit at import time.
constexpr int g_test_sample_rate = 48000;

// Test fixtures use stereo audio because that is the dominant backing-audio shape and exercises
// the analyzer's multi-channel interleave path.
constexpr int g_test_channels = 2;

// Duration used by every generated WAV fixture. One second is long enough for libebur128's gating
// to settle without making tests slow.
constexpr double g_test_duration_seconds = 1.0;

// Owns a clean temporary directory for audio normalization test fixtures.
class TemporaryAudioDirectory final
{
public:
    // Creates a unique temporary directory under the platform temp root.
    TemporaryAudioDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-audio-normalization-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the temp directory on a best-effort basis even if the test exited unwound.
    ~TemporaryAudioDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        catch (...)
        {
            m_path.clear();
        }
    }

    TemporaryAudioDirectory(const TemporaryAudioDirectory&) = delete;
    TemporaryAudioDirectory& operator=(const TemporaryAudioDirectory&) = delete;
    TemporaryAudioDirectory(TemporaryAudioDirectory&&) = delete;
    TemporaryAudioDirectory& operator=(TemporaryAudioDirectory&&) = delete;

    // Returns the temporary root used by this test.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Temporary root removed by the destructor after each test.
    std::filesystem::path m_path;
};

// Writes a stereo sine-wave WAV fixture at the requested amplitude. Returns the produced path so
// callers can chain it into normalization calls without re-deriving the filename.
[[nodiscard]] std::filesystem::path writeSineWaveWav(
    const std::filesystem::path& path, double amplitude, double frequency_hz = 1000.0)
{
    const int total_samples = static_cast<int>(g_test_duration_seconds * g_test_sample_rate);
    juce::AudioBuffer<float> buffer{g_test_channels, total_samples};
    buffer.clear();
    for (int sample = 0; sample < total_samples; ++sample)
    {
        const double t = static_cast<double>(sample) / g_test_sample_rate;
        const auto value =
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency_hz * t));
        for (int channel = 0; channel < g_test_channels; ++channel)
        {
            buffer.setSample(channel, sample, value);
        }
    }

    juce::WavAudioFormat wav_format;
    auto stream = std::make_unique<juce::FileOutputStream>(common::core::juceFileFromPath(path));
    REQUIRE(!stream->failedToOpen());
    REQUIRE(stream->setPosition(0));
    REQUIRE(!stream->truncate().failed());
    std::unique_ptr<juce::OutputStream> output_stream{std::move(stream)};
    const auto writer_options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(g_test_sample_rate)
            .withNumChannels(g_test_channels)
            .withBitsPerSample(24)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral);
    std::unique_ptr<juce::AudioFormatWriter> writer =
        wav_format.createWriterFor(output_stream, writer_options);
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, total_samples));
    return path;
}

// Writes a fixture filled with zero samples to drive the silent-input branch.
[[nodiscard]] std::filesystem::path writeSilentWav(const std::filesystem::path& path)
{
    return writeSineWaveWav(path, 0.0);
}

} // namespace

// Verifies analyzeAudioForGainNormalization surfaces InputFileMissing for a missing file.
TEST_CASE(
    "analyzeAudioForGainNormalization returns InputFileMissing",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto result = analyzeAudioForGainNormalization(
        temporary_directory.path() / "nope.wav", common::core::AudioNormalizationTarget{});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::InputFileMissing);
}

// Verifies silent inputs fail with SilentInputCannotBeNormalized instead of producing nonsense
// gain.
TEST_CASE(
    "analyzeAudioForGainNormalization rejects silent input", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSilentWav(temporary_directory.path() / "input.wav");

    const auto result =
        analyzeAudioForGainNormalization(input_path, common::core::AudioNormalizationTarget{});

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::SilentInputCannotBeNormalized);
}

// Verifies analyzeAudioForGainNormalization computes a non-zero gain and a 64-char validation hash.
TEST_CASE(
    "analyzeAudioForGainNormalization computes gain and validation hash",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto result =
        analyzeAudioForGainNormalization(input_path, common::core::AudioNormalizationTarget{});

    REQUIRE(result.has_value());
    CHECK(result->gain_db != 0.0);
    CHECK(result->validation_sha256.size() == 64);
}

// Verifies validateAudioNormalization confirms a freshly computed normalization record.
TEST_CASE(
    "validateAudioNormalization confirms fresh analysis", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto normalization =
        analyzeAudioForGainNormalization(input_path, common::core::AudioNormalizationTarget{});

    REQUIRE(normalization.has_value());
    CHECK(validateAudioNormalization(input_path, *normalization));
}

// Verifies validateAudioNormalization rejects a normalization record with a tampered gain.
TEST_CASE("validateAudioNormalization rejects tampered gain", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    auto normalization =
        analyzeAudioForGainNormalization(input_path, common::core::AudioNormalizationTarget{});

    REQUIRE(normalization.has_value());
    normalization->gain_db += 1.0;
    CHECK_FALSE(validateAudioNormalization(input_path, *normalization));
}

// Verifies the validation hash covers the raw backing audio bytes, not only the stored gain.
TEST_CASE(
    "validateAudioNormalization rejects changed audio bytes", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto normalization =
        analyzeAudioForGainNormalization(input_path, common::core::AudioNormalizationTarget{});

    REQUIRE(normalization.has_value());
    [[maybe_unused]] const auto overwritten_path = writeSineWaveWav(input_path, 0.25);
    CHECK_FALSE(validateAudioNormalization(input_path, *normalization));
}

// Verifies validateAudioNormalization returns false for a missing input file.
TEST_CASE(
    "validateAudioNormalization returns false for missing input",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const common::core::AudioNormalization normalization{
        .gain_db = -4.0,
        .validation_sha256 = std::string(64, 'a'),
    };

    CHECK_FALSE(
        validateAudioNormalization(temporary_directory.path() / "missing.wav", normalization));
}

} // namespace rock_hero::common::audio
