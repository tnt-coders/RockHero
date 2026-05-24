#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <numbers>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/audio_loudness_metadata.h>
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

// Target used by every test that runs normalization. Peak clamping is implicit at 0 dBFS.
constexpr common::core::AudioNormalizationTarget g_test_target{
    .integrated_loudness_lufs = -16.0,
};

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

// Converts std::filesystem paths to JUCE paths while preserving Windows wide paths.
[[nodiscard]] juce::File juceFileFromPath(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return juce::File{juce::String{path.wstring().c_str()}};
#else
    return juce::File{juce::String::fromUTF8(path.string().c_str())};
#endif
}

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
    auto stream = std::make_unique<juce::FileOutputStream>(juceFileFromPath(path));
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

// Writes a fixture that pairs a brief high-amplitude transient with a long quiet bed so the
// integrated loudness stays well below the normalization target while sample peak leaves little
// headroom. Used to exercise the gain-clamping branch.
[[nodiscard]] std::filesystem::path writePeakHeavyWav(const std::filesystem::path& path)
{
    const int total_samples = static_cast<int>(g_test_duration_seconds * g_test_sample_rate);
    juce::AudioBuffer<float> buffer{g_test_channels, total_samples};
    buffer.clear();
    constexpr double quiet_amplitude = 0.02;
    constexpr double transient_amplitude = 0.95;
    constexpr int transient_samples = 64;
    for (int sample = 0; sample < total_samples; ++sample)
    {
        const double t = static_cast<double>(sample) / g_test_sample_rate;
        const double base = quiet_amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * t);
        const double value = sample < transient_samples ? transient_amplitude : base;
        for (int channel = 0; channel < g_test_channels; ++channel)
        {
            buffer.setSample(channel, sample, static_cast<float>(value));
        }
    }

    juce::WavAudioFormat wav_format;
    auto stream = std::make_unique<juce::FileOutputStream>(juceFileFromPath(path));
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

// Verifies the analysis boundary surfaces a stable error when the input file does not exist.
TEST_CASE(
    "measureAudioLoudness returns InputFileMissing for missing input",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto result = measureAudioLoudness(temporary_directory.path() / "does_not_exist.wav");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::InputFileMissing);
}

// Verifies the normalization boundary surfaces InputFileMissing before any render work runs.
TEST_CASE(
    "normalizeAudioFile returns InputFileMissing for missing input",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto result = normalizeAudioFile(
        temporary_directory.path() / "does_not_exist.wav",
        temporary_directory.path() / "output.wav",
        g_test_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::InputFileMissing);
}

// Verifies the normalization boundary rejects empty output paths before doing any I/O.
TEST_CASE(
    "normalizeAudioFile returns OutputPathRequired for empty output",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto result = normalizeAudioFile(input_path, {}, g_test_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::OutputPathRequired);
}

// Verifies measureAudioLoudness reads back deterministic measurement and fingerprint values.
TEST_CASE(
    "measureAudioLoudness reports measurement and fingerprint",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto result = measureAudioLoudness(input_path);

    REQUIRE(result.has_value());
    CHECK(result->measurement.sample_peak_dbfs <= 0.0);
    CHECK(result->fingerprint.size_bytes > 0);
    CHECK(result->fingerprint.sha256.size() == 64);
    CHECK_FALSE(result->analyzer_id.empty());
    CHECK_FALSE(result->analyzer_version.empty());
}

// Verifies a sine louder than the target is rendered quieter and reports the negative gain.
TEST_CASE(
    "normalizeAudioFile quietens loud input toward target", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);
    const auto output_path = temporary_directory.path() / "output.wav";

    const auto outcome = normalizeAudioFile(input_path, output_path, g_test_target);

    REQUIRE(outcome.has_value());
    CHECK(outcome->applied_gain_db < 0.0);
    CHECK(std::filesystem::is_regular_file(output_path));
    CHECK_FALSE(
        std::filesystem::exists(std::filesystem::path{output_path}.replace_extension(".wav.tmp")));

    const auto rendered = measureAudioLoudness(output_path);
    REQUIRE(rendered.has_value());
    CHECK(
        std::abs(
            rendered->measurement.integrated_loudness_lufs -
            g_test_target.integrated_loudness_lufs) < 1.0);
}

// Verifies a quiet sine is rendered louder and the applied gain is positive.
TEST_CASE(
    "normalizeAudioFile boosts quiet input toward target", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.05);
    const auto output_path = temporary_directory.path() / "output.wav";

    const auto outcome = normalizeAudioFile(input_path, output_path, g_test_target);

    REQUIRE(outcome.has_value());
    CHECK(outcome->applied_gain_db > 0.0);

    const auto rendered = measureAudioLoudness(output_path);
    REQUIRE(rendered.has_value());
    CHECK(
        std::abs(
            rendered->measurement.integrated_loudness_lufs -
            g_test_target.integrated_loudness_lufs) < 1.0);
}

// Verifies gain is clamped so the loudest sample does not exceed 0 dBFS after gain.
TEST_CASE(
    "analyzeAudioForGainNormalization clamps gain at sample peak",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writePeakHeavyWav(temporary_directory.path() / "input.wav");

    const auto metadata = analyzeAudioForGainNormalization(input_path, g_test_target);

    REQUIRE(metadata.has_value());
    // The gain should be clamped so peak + gain <= 0 dBFS.
    CHECK(metadata->analysis.measurement.sample_peak_dbfs + metadata->applied_gain_db <= 0.01);
}

// Verifies silent inputs fail with SilentInputCannotBeNormalized instead of producing nonsense gain.
TEST_CASE(
    "analyzeAudioForGainNormalization rejects silent input", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSilentWav(temporary_directory.path() / "input.wav");

    const auto result = analyzeAudioForGainNormalization(input_path, g_test_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::SilentInputCannotBeNormalized);
}

// Verifies analyzeAudioForGainNormalization surfaces InputFileMissing for a missing file.
TEST_CASE(
    "analyzeAudioForGainNormalization returns InputFileMissing",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto result =
        analyzeAudioForGainNormalization(temporary_directory.path() / "nope.wav", g_test_target);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioNormalizationErrorCode::InputFileMissing);
}

// Verifies analyzeAudioForGainNormalization computes the expected gain for a known input.
TEST_CASE(
    "analyzeAudioForGainNormalization computes applied gain", "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);

    const auto metadata = analyzeAudioForGainNormalization(input_path, g_test_target);

    REQUIRE(metadata.has_value());
    CHECK(metadata->applied_gain_db != 0.0);
    CHECK(metadata->target == g_test_target);
    CHECK(metadata->analysis.fingerprint.size_bytes > 0);
    CHECK(metadata->analysis.fingerprint.sha256.size() == 64);
    CHECK_FALSE(metadata->analysis.analyzer_id.empty());
}

// Verifies the rendered output's persisted metadata matches a fresh re-measurement of the file.
TEST_CASE(
    "normalizeAudioFile metadata matches fresh re-measurement",
    "[common-audio][audio-normalization]")
{
    const TemporaryAudioDirectory temporary_directory;
    const auto input_path = writeSineWaveWav(temporary_directory.path() / "input.wav", 0.5);
    const auto output_path = temporary_directory.path() / "output.wav";

    const auto outcome = normalizeAudioFile(input_path, output_path, g_test_target);

    REQUIRE(outcome.has_value());
    const auto fresh = measureAudioLoudness(output_path);
    REQUIRE(fresh.has_value());
    CHECK(
        std::abs(
            outcome->metadata.analysis.measurement.integrated_loudness_lufs -
            fresh->measurement.integrated_loudness_lufs) < 0.1);
    CHECK(outcome->metadata.analysis.fingerprint == fresh->fingerprint);
    CHECK(outcome->metadata.analysis.analyzer_id == fresh->analyzer_id);
    CHECK(outcome->metadata.analysis.analyzer_version == fresh->analyzer_version);
    CHECK(outcome->metadata.target == g_test_target);
}

} // namespace rock_hero::common::audio
