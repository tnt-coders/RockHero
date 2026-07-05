#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ebur128.h>
#include <filesystem>
#include <format>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <limits>
#include <memory>
#include <rock_hero/common/audio/song/audio_normalization.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Block size used by the analyzer scan. Tuned for cache-friendly reads while keeping the
// per-channel buffer small enough that interleaving stays cheap.
constexpr int g_block_frames = 8192;

// Loudness below this floor is treated as silent for the purposes of normalization. Anything
// quieter has too little signal energy for libebur128 to produce a reliable integrated value.
constexpr double g_silent_loudness_threshold_lufs = -70.0;

// Version marker baked into the validation hash so changing the normalization algorithm (e.g.
// target LUFS, gain formula, or peak strategy) can invalidate all existing hashes by bumping
// the version number.
constexpr const char* g_hash_version = "RockHeroNormalizationV1";

// Buffer size used between JUCE's 64-byte SHA-256 block reader and the backing file stream.
constexpr int g_hash_stream_buffer_bytes = 65536;

// Frees libebur128 state through the library's pointer-to-pointer destroy contract.
struct Ebur128StateDeleter
{
    void operator()(ebur128_state* state) const noexcept
    {
        if (state != nullptr)
        {
            ebur128_state* local = state;
            ebur128_destroy(&local);
        }
    }
};
using Ebur128StateOwner = std::unique_ptr<ebur128_state, Ebur128StateDeleter>;

// Reports whether a JUCE reader's channel count is supported by the analyzer.
[[nodiscard]] bool isSupportedChannelCount(unsigned int channels) noexcept
{
    return channels == 1 || channels == 2;
}

// Measurement values produced by the LUFS-I + sample peak analyzer. Runtime-only; not persisted.
struct LoudnessMeasurement
{
    double integrated_loudness_lufs{0.0};
    double sample_peak_dbfs{0.0};
};

// Streams decoded blocks from a JUCE reader through libebur128 to produce a single measurement.
[[nodiscard]] std::expected<LoudnessMeasurement, AudioNormalizationError> runLoudnessAnalyzer(
    juce::AudioFormatReader& reader)
{
    const auto channels = static_cast<unsigned int>(reader.numChannels);
    if (!isSupportedChannelCount(channels))
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::UnsupportedInputFormat,
            "Loudness analyzer only supports mono or stereo input",
        }};
    }

    const Ebur128StateOwner state{ebur128_init(
        channels,
        static_cast<unsigned long>(reader.sampleRate),
        EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK)};
    if (state == nullptr)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::LoudnessMeasurementFailed,
            "Could not initialize loudness analyzer",
        }};
    }

    juce::AudioBuffer<float> buffer{static_cast<int>(channels), g_block_frames};
    // Interleaved scratch buffer reused across blocks; libebur128_add_frames_float requires
    // interleaved input even when the source reader produces planar channels.
    std::vector<float> interleaved(static_cast<std::size_t>(channels) * g_block_frames);

    std::int64_t sample_pos = 0;
    const std::int64_t total_samples = reader.lengthInSamples;
    while (sample_pos < total_samples)
    {
        const int frames_this_block =
            static_cast<int>(std::min<std::int64_t>(g_block_frames, total_samples - sample_pos));
        buffer.clear();
        if (!reader.read(
                buffer.getArrayOfWritePointers(),
                static_cast<int>(channels),
                sample_pos,
                frames_this_block))
        {
            return std::unexpected{AudioNormalizationError{
                AudioNormalizationErrorCode::InvalidInputAudio,
                "Could not read input audio block during loudness analysis",
            }};
        }

        for (int frame = 0; frame < frames_this_block; ++frame)
        {
            for (unsigned int ch = 0; ch < channels; ++ch)
            {
                interleaved[static_cast<std::size_t>(frame) * channels + ch] =
                    buffer.getSample(static_cast<int>(ch), frame);
            }
        }

        if (ebur128_add_frames_float(
                state.get(), interleaved.data(), static_cast<std::size_t>(frames_this_block)) !=
            EBUR128_SUCCESS)
        {
            return std::unexpected{AudioNormalizationError{
                AudioNormalizationErrorCode::LoudnessMeasurementFailed,
                "Loudness analyzer rejected input block",
            }};
        }

        sample_pos += frames_this_block;
    }

    double integrated = 0.0;
    if (ebur128_loudness_global(state.get(), &integrated) != EBUR128_SUCCESS)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::LoudnessMeasurementFailed,
            "Could not compute integrated loudness",
        }};
    }

    double max_sample_peak_linear = 0.0;
    for (unsigned int ch = 0; ch < channels; ++ch)
    {
        double channel_peak_linear = 0.0;
        if (ebur128_sample_peak(state.get(), ch, &channel_peak_linear) != EBUR128_SUCCESS)
        {
            return std::unexpected{AudioNormalizationError{
                AudioNormalizationErrorCode::LoudnessMeasurementFailed,
                "Could not compute sample peak",
            }};
        }
        max_sample_peak_linear = std::max(max_sample_peak_linear, channel_peak_linear);
    }

    const double sample_peak_dbfs = max_sample_peak_linear > 0.0
                                        ? 20.0 * std::log10(max_sample_peak_linear)
                                        : -std::numeric_limits<double>::infinity();

    return LoudnessMeasurement{
        .integrated_loudness_lufs = integrated,
        .sample_peak_dbfs = sample_peak_dbfs,
    };
}

// Rounds a gain value to one decimal place (0.1 dB precision).
[[nodiscard]] double roundGainToOneDecimal(double gain_db) noexcept
{
    return std::round(gain_db * 10.0) / 10.0;
}

// Formats a gain value at one decimal place for use in the validation hash.
[[nodiscard]] std::string formatGainForHash(double gain_db)
{
    return std::format("{:.1f}", gain_db);
}

// Presents the hash prefix and audio file as one stream so SHA-256 does not need a full-file
// MemoryBlock. The worker still reads the whole file, but it no longer allocates and copies the
// complete backing track before hashing.
class ValidationHashInputStream final : public juce::InputStream
{
public:
    ValidationHashInputStream(std::string prefix, const juce::File& audio_file)
        : m_prefix(std::move(prefix))
        , m_file_stream(audio_file)
        , m_file_length(std::max<juce::int64>(0, m_file_stream.getTotalLength()))
    {}

    [[nodiscard]] bool failedToOpen() const noexcept
    {
        return m_file_stream.failedToOpen();
    }

    [[nodiscard]] std::string errorMessage() const
    {
        return m_file_stream.getStatus().getErrorMessage().toStdString();
    }

    [[nodiscard]] juce::int64 getTotalLength() override
    {
        return prefixLength() + m_file_length;
    }

    [[nodiscard]] bool isExhausted() override
    {
        return m_position >= getTotalLength();
    }

    int read(void* destination_buffer, int max_bytes_to_read) override
    {
        if (destination_buffer == nullptr || max_bytes_to_read <= 0)
        {
            return 0;
        }

        auto* output = static_cast<char*>(destination_buffer);
        int bytes_read = 0;
        const juce::int64 prefix_length = prefixLength();

        if (m_position < prefix_length)
        {
            const auto prefix_position = static_cast<std::size_t>(m_position);
            const int remaining_prefix_bytes = static_cast<int>(prefix_length - m_position);
            const int prefix_bytes_to_read = std::min(max_bytes_to_read, remaining_prefix_bytes);

            std::memcpy(
                output,
                m_prefix.data() + prefix_position,
                static_cast<std::size_t>(prefix_bytes_to_read));

            output += prefix_bytes_to_read;
            max_bytes_to_read -= prefix_bytes_to_read;
            bytes_read += prefix_bytes_to_read;
            m_position += prefix_bytes_to_read;
        }

        if (max_bytes_to_read <= 0)
        {
            return bytes_read;
        }

        const int file_bytes_read = m_file_stream.read(output, max_bytes_to_read);
        if (file_bytes_read <= 0)
        {
            return bytes_read;
        }

        m_position += file_bytes_read;
        return bytes_read + file_bytes_read;
    }

    [[nodiscard]] juce::int64 getPosition() override
    {
        return m_position;
    }

    bool setPosition(juce::int64 new_position) override
    {
        if (new_position < 0)
        {
            return false;
        }

        const juce::int64 prefix_length = prefixLength();
        if (new_position < prefix_length)
        {
            if (!m_file_stream.setPosition(0))
            {
                return false;
            }
            m_position = new_position;
            return true;
        }

        if (!m_file_stream.setPosition(new_position - prefix_length))
        {
            return false;
        }

        m_position = prefix_length + m_file_stream.getPosition();
        return m_position == new_position;
    }

private:
    [[nodiscard]] juce::int64 prefixLength() const noexcept
    {
        return static_cast<juce::int64>(m_prefix.size());
    }

    std::string m_prefix;
    juce::FileInputStream m_file_stream;
    juce::int64 m_file_length{0};
    juce::int64 m_position{0};
};

// Computes the validation hash from the version prefix, gain text, and raw audio file bytes.
// The hash input is:
//   RockHeroNormalizationV1\n
//   gainDb=<one-decimal text>\n
//   audioBytes\n
//   <raw file bytes>
[[nodiscard]] std::expected<std::string, AudioNormalizationError> computeValidationHash(
    const std::filesystem::path& audio_path, double gain_db)
{
    const std::string gain_text = formatGainForHash(gain_db);
    std::string prefix = std::string{g_hash_version} + "\ngainDb=" + gain_text + "\naudioBytes\n";

    ValidationHashInputStream hash_input{
        std::move(prefix), common::core::juceFileFromPath(audio_path)
    };
    if (hash_input.failedToOpen())
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::ValidationHashFailed,
            "Could not open file for validation hashing: " + hash_input.errorMessage(),
        }};
    }

    juce::BufferedInputStream buffered_hash_input{hash_input, g_hash_stream_buffer_bytes};
    const juce::SHA256 hash{buffered_hash_input};
    return hash.toHexString().toLowerCase().toStdString();
}

// Centralises default error messages so AudioNormalizationError stays consistent regardless of
// which call site reports a failure code.
[[nodiscard]] std::string defaultAudioNormalizationErrorMessage(AudioNormalizationErrorCode code)
{
    switch (code)
    {
        case AudioNormalizationErrorCode::InputFileMissing:
        {
            return "Input audio file does not exist";
        }
        case AudioNormalizationErrorCode::CouldNotOpenInput:
        {
            return "Could not open input audio file";
        }
        case AudioNormalizationErrorCode::UnsupportedInputFormat:
        {
            return "Input audio format is not supported";
        }
        case AudioNormalizationErrorCode::InvalidInputAudio:
        {
            return "Input audio is invalid or could not be read";
        }
        case AudioNormalizationErrorCode::LoudnessMeasurementFailed:
        {
            return "Loudness measurement failed";
        }
        case AudioNormalizationErrorCode::SilentInputCannotBeNormalized:
        {
            return "Input audio is effectively silent and cannot be normalized";
        }
        case AudioNormalizationErrorCode::ValidationHashFailed:
        {
            return "Could not compute validation hash for audio file";
        }
    }

    return "Audio normalization failed";
}

} // namespace

AudioNormalizationError::AudioNormalizationError(AudioNormalizationErrorCode error_code)
    : AudioNormalizationError(error_code, defaultAudioNormalizationErrorMessage(error_code))
{}

AudioNormalizationError::AudioNormalizationError(
    AudioNormalizationErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

// Public boundary: analyzes a source file, computes gain, and produces validation hash.
std::expected<common::core::AudioNormalization, AudioNormalizationError>
analyzeAudioForGainNormalization(
    const std::filesystem::path& input, const common::core::AudioNormalizationTarget& target)
{
    if (input.empty())
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::InputFileMissing,
        }};
    }

    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(input, filesystem_error))
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::InputFileMissing,
        }};
    }

    juce::AudioFormatManager format_manager;
    format_manager.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>{format_manager.createReaderFor(
        common::core::juceFileFromPath(input))};
    if (reader == nullptr)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::CouldNotOpenInput,
        }};
    }

    auto measurement = runLoudnessAnalyzer(*reader);
    if (!measurement.has_value())
    {
        return std::unexpected{std::move(measurement.error())};
    }

    if (!std::isfinite(measurement->integrated_loudness_lufs) ||
        measurement->integrated_loudness_lufs < g_silent_loudness_threshold_lufs)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::SilentInputCannotBeNormalized,
        }};
    }

    // Compute gain clamped so the loudest sample after gain does not exceed 0 dBFS.
    const double desired_gain_db =
        target.integrated_loudness_lufs - measurement->integrated_loudness_lufs;
    const double peak_headroom_db = 0.0 - measurement->sample_peak_dbfs;
    const double gain_db = roundGainToOneDecimal(std::min(desired_gain_db, peak_headroom_db));

    auto validation_sha256 = computeValidationHash(input, gain_db);
    if (!validation_sha256.has_value())
    {
        return std::unexpected{std::move(validation_sha256.error())};
    }

    return common::core::AudioNormalization{
        .gain_db = gain_db,
        .validation_sha256 = std::move(*validation_sha256),
    };
}

// Public boundary: validates stored normalization against the current audio file.
bool validateAudioNormalization(
    const std::filesystem::path& input, const common::core::AudioNormalization& normalization)
{
    if (normalization.validation_sha256.empty())
    {
        return false;
    }

    const auto current_hash = computeValidationHash(input, normalization.gain_db);
    if (!current_hash.has_value())
    {
        return false;
    }

    return *current_hash == normalization.validation_sha256;
}

} // namespace rock_hero::common::audio
