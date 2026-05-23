#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ebur128.h>
#include <filesystem>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <limits>
#include <memory>
#include <rock_hero/common/audio/audio_normalization.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Block size used by both the analyzer scan and the render pass. Tuned for cache-friendly reads
// while keeping the per-channel buffer small enough that interleaving stays cheap.
constexpr int g_block_frames = 8192;

// Bit depth used by the canonical normalized WAV writer. 24-bit PCM keeps file size in line with
// typical backing audio while staying well above the analyzer's measurement noise floor.
constexpr int g_output_bit_depth = 24;

// Loudness below this floor is treated as silent for the purposes of normalization. Anything
// quieter has too little signal energy for libebur128 to produce a reliable integrated value.
constexpr double g_silent_loudness_threshold_lufs = -70.0;

// Stable analyzer identifier persisted on AudioLoudnessAnalysis. Editor-side staleness checks
// treat measurements with a different analyzer_id as incomparable.
constexpr const char* g_analyzer_id = "libebur128";

// Converts std::filesystem paths to JUCE paths while preserving Windows wide paths.
[[nodiscard]] juce::File juceFileFromPath(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return juce::File{juce::String{path.wstring().c_str()}};
#else
    return juce::File{juce::String::fromUTF8(path.string().c_str())};
#endif
}

// Reads libebur128's runtime version so persisted measurements record the exact library revision
// they were produced with rather than a hard-coded constant.
[[nodiscard]] std::string libebur128Version()
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    ebur128_get_version(&major, &minor, &patch);
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

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

// Registers the JUCE formats that current backing-audio imports produce. AudioFormatManager is
// non-copyable, so callers pass an existing instance by reference rather than constructing one
// through a return-by-value helper.
void registerBasicAudioFormats(juce::AudioFormatManager& format_manager)
{
    format_manager.registerBasicFormats();
}

// Reports whether a JUCE reader's channel count is supported by the first-pass normalizer.
[[nodiscard]] bool isSupportedChannelCount(unsigned int channels) noexcept
{
    return channels == 1 || channels == 2;
}

// Opens a reader on the supplied audio file. The caller treats any null result as a failure mode
// distinct from "the format is supported but the audio is malformed".
[[nodiscard]] std::unique_ptr<juce::AudioFormatReader> openAudioReader(
    juce::AudioFormatManager& format_manager, const std::filesystem::path& input_path)
{
    return std::unique_ptr<juce::AudioFormatReader>{format_manager.createReaderFor(
        juceFileFromPath(input_path))};
}

// Streams decoded blocks from a JUCE reader through libebur128 to produce a single measurement.
// Returns an error code on analyzer failure so callers can map it to AudioNormalizationError.
[[nodiscard]] std::optional<common::core::AudioLoudnessMeasurement> runLoudnessAnalyzer(
    juce::AudioFormatReader& reader, AudioNormalizationErrorCode& error_code,
    std::string& error_message)
{
    const auto channels = static_cast<unsigned int>(reader.numChannels);
    if (!isSupportedChannelCount(channels))
    {
        error_code = AudioNormalizationErrorCode::UnsupportedInputFormat;
        error_message = "Loudness analyzer only supports mono or stereo input";
        return std::nullopt;
    }

    Ebur128StateOwner state{ebur128_init(
        channels,
        static_cast<unsigned long>(reader.sampleRate),
        EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK)};
    if (state == nullptr)
    {
        error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
        error_message = "Could not initialize loudness analyzer";
        return std::nullopt;
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
            error_code = AudioNormalizationErrorCode::InvalidInputAudio;
            error_message = "Could not read input audio block during loudness analysis";
            return std::nullopt;
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
            error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
            error_message = "Loudness analyzer rejected input block";
            return std::nullopt;
        }

        sample_pos += frames_this_block;
    }

    double integrated = 0.0;
    if (ebur128_loudness_global(state.get(), &integrated) != EBUR128_SUCCESS)
    {
        error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
        error_message = "Could not compute integrated loudness";
        return std::nullopt;
    }

    double max_true_peak_linear = 0.0;
    for (unsigned int ch = 0; ch < channels; ++ch)
    {
        double channel_peak_linear = 0.0;
        if (ebur128_true_peak(state.get(), ch, &channel_peak_linear) != EBUR128_SUCCESS)
        {
            error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
            error_message = "Could not compute true peak";
            return std::nullopt;
        }
        max_true_peak_linear = std::max(max_true_peak_linear, channel_peak_linear);
    }

    // libebur128 reports linear amplitude for peaks; convert to dBTP so persisted metadata stays
    // in the same units as the configured peak ceiling.
    const double true_peak_dbtp = max_true_peak_linear > 0.0
                                      ? 20.0 * std::log10(max_true_peak_linear)
                                      : -std::numeric_limits<double>::infinity();

    return common::core::AudioLoudnessMeasurement{
        .integrated_loudness_lufs = integrated,
        .true_peak_dbtp = true_peak_dbtp,
    };
}

// Produces a size + SHA-256 fingerprint of an existing file. Size comes from std::filesystem so
// the cost is constant; the hash is computed by streaming the file through juce::SHA256.
[[nodiscard]] std::optional<common::core::AudioFileFingerprint> fingerprintAudioFile(
    const std::filesystem::path& path, std::string& error_message)
{
    std::error_code filesystem_error;
    const std::uintmax_t file_size = std::filesystem::file_size(path, filesystem_error);
    if (filesystem_error)
    {
        error_message = "Could not read file size: " + filesystem_error.message();
        return std::nullopt;
    }

    juce::FileInputStream input_stream{juceFileFromPath(path)};
    if (input_stream.failedToOpen())
    {
        error_message = "Could not open file for hashing: " +
                        input_stream.getStatus().getErrorMessage().toStdString();
        return std::nullopt;
    }

    const juce::SHA256 hash{input_stream};
    return common::core::AudioFileFingerprint{
        .size_bytes = static_cast<std::uint64_t>(file_size),
        .sha256 = hash.toHexString().toLowerCase().toStdString(),
    };
}

// Pure helper for the gain formula. Returned gain is dB; limited flag is true when the peak
// ceiling forced the gain lower than the requested LUFS delta.
struct NormalizationGain
{
    double gain_db{0.0};
    bool limited_by_peak_ceiling{false};
};
[[nodiscard]] NormalizationGain calculateNormalizationGainDb(
    const common::core::AudioLoudnessMeasurement& source,
    const common::core::AudioNormalizationTarget& target) noexcept
{
    const double desired_gain_db =
        target.integrated_loudness_lufs - source.integrated_loudness_lufs;
    const double peak_limited_gain_db = target.true_peak_ceiling_dbtp - source.true_peak_dbtp;
    const double applied = std::min(desired_gain_db, peak_limited_gain_db);
    return NormalizationGain{
        .gain_db = applied,
        .limited_by_peak_ceiling = applied < desired_gain_db,
    };
}

// Streams the reader's audio through a 24-bit WAV writer applying a single uniform gain. Uses a
// temporary stream-owning scope so the writer's destructor flushes before validation begins.
[[nodiscard]] bool renderNormalizedAudioFile(
    juce::AudioFormatReader& reader, const std::filesystem::path& output_path, double gain_linear,
    AudioNormalizationErrorCode& error_code, std::string& error_message)
{
    std::error_code filesystem_error;
    if (!output_path.parent_path().empty())
    {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error_code = AudioNormalizationErrorCode::CouldNotCreateOutputDirectory;
            error_message = "Could not create output directory: " + filesystem_error.message();
            return false;
        }
    }

    auto file_stream = std::make_unique<juce::FileOutputStream>(juceFileFromPath(output_path));
    if (file_stream->failedToOpen())
    {
        error_code = AudioNormalizationErrorCode::CouldNotCreateOutputFile;
        error_message = "Could not open output file for writing: " +
                        file_stream->getStatus().getErrorMessage().toStdString();
        return false;
    }
    if (!file_stream->setPosition(0) || file_stream->truncate().failed())
    {
        error_code = AudioNormalizationErrorCode::CouldNotCreateOutputFile;
        error_message = "Could not truncate output file before writing";
        return false;
    }

    juce::WavAudioFormat wav_format;
    juce::FileOutputStream* const stream_ptr = file_stream.get();
    std::unique_ptr<juce::AudioFormatWriter> writer{wav_format.createWriterFor(
        stream_ptr,
        reader.sampleRate,
        static_cast<unsigned int>(reader.numChannels),
        g_output_bit_depth,
        juce::StringPairArray{},
        0)};
    if (writer == nullptr)
    {
        // createWriterFor returns nullptr on failure and does NOT take ownership of the stream
        // in that case, so the unique_ptr above keeps freeing the stream for us.
        error_code = AudioNormalizationErrorCode::CouldNotCreateOutputFile;
        error_message = "Could not create WAV writer for output file";
        return false;
    }
    // On success the writer takes ownership of the stream. Release the unique_ptr so it does
    // not double-free.
    file_stream.release();

    const auto channel_count = static_cast<int>(reader.numChannels);
    juce::AudioBuffer<float> buffer{channel_count, g_block_frames};
    std::int64_t sample_pos = 0;
    const std::int64_t total_samples = reader.lengthInSamples;
    while (sample_pos < total_samples)
    {
        const int frames_this_block =
            static_cast<int>(std::min<std::int64_t>(g_block_frames, total_samples - sample_pos));
        buffer.clear();
        if (!reader.read(
                buffer.getArrayOfWritePointers(), channel_count, sample_pos, frames_this_block))
        {
            error_code = AudioNormalizationErrorCode::InvalidInputAudio;
            error_message = "Could not read input audio block during render";
            return false;
        }

        buffer.applyGain(0, frames_this_block, static_cast<float>(gain_linear));

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, frames_this_block))
        {
            error_code = AudioNormalizationErrorCode::OutputRenderFailed;
            error_message = "Could not write output audio block";
            return false;
        }

        sample_pos += frames_this_block;
    }

    return true;
}

// Reopens a rendered output file, runs a fresh measurement, and fingerprints it. The returned
// analysis is what the caller persists on the output AudioAsset's loudness metadata. Opens
// through WavAudioFormat directly rather than the format manager because the temporary output
// path ends in ".tmp" — AudioFormatManager dispatches by file extension and would not match a
// recognized format for that suffix, returning nullptr even though we just wrote a valid WAV.
[[nodiscard]] std::optional<common::core::AudioLoudnessAnalysis> validateRenderedAudioFile(
    const std::filesystem::path& output_path, AudioNormalizationErrorCode& error_code,
    std::string& error_message)
{
    auto file_stream = std::make_unique<juce::FileInputStream>(juceFileFromPath(output_path));
    if (file_stream->failedToOpen())
    {
        error_code = AudioNormalizationErrorCode::OutputValidationFailed;
        error_message = "Could not open rendered output for validation: " +
                        file_stream->getStatus().getErrorMessage().toStdString();
        return std::nullopt;
    }

    juce::WavAudioFormat wav_format;
    juce::FileInputStream* const stream_ptr = file_stream.get();
    std::unique_ptr<juce::AudioFormatReader> reader{wav_format.createReaderFor(
        stream_ptr, /*deleteStreamIfOpeningFails=*/false)};
    if (reader == nullptr)
    {
        // createReaderFor returned null and we passed false, so the unique_ptr above still owns
        // the stream and will free it.
        error_code = AudioNormalizationErrorCode::OutputValidationFailed;
        error_message = "Could not parse rendered output as a WAV file";
        return std::nullopt;
    }
    // On success the reader took ownership of the stream.
    file_stream.release();

    auto measurement = runLoudnessAnalyzer(*reader, error_code, error_message);
    if (!measurement.has_value())
    {
        // Promote any analyzer failure during validation to OutputValidationFailed so callers
        // see a single code regardless of which underlying read or analyzer call failed.
        error_code = AudioNormalizationErrorCode::OutputValidationFailed;
        return std::nullopt;
    }

    auto fingerprint = fingerprintAudioFile(output_path, error_message);
    if (!fingerprint.has_value())
    {
        error_code = AudioNormalizationErrorCode::OutputValidationFailed;
        return std::nullopt;
    }

    return common::core::AudioLoudnessAnalysis{
        .measurement = *measurement,
        .fingerprint = *std::move(fingerprint),
        .analyzer_id = g_analyzer_id,
        .analyzer_version = libebur128Version(),
    };
}

// Removes a temporary or partially-written output file on failure. Cleanup errors are surfaced
// so the caller can report them, but they do not override the original failure code.
void removePartialOutputOnFailure(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

// Builds a sibling .tmp path so the canonical output is replaced atomically only after the
// rendered file has been validated.
[[nodiscard]] std::filesystem::path temporaryOutputPath(const std::filesystem::path& output)
{
    std::filesystem::path temporary_path = output;
    temporary_path += ".tmp";
    return temporary_path;
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
        case AudioNormalizationErrorCode::OutputPathRequired:
        {
            return "Output audio file path is required";
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
        case AudioNormalizationErrorCode::CouldNotCreateOutputDirectory:
        {
            return "Could not create output audio directory";
        }
        case AudioNormalizationErrorCode::CouldNotCreateOutputFile:
        {
            return "Could not create output audio file";
        }
        case AudioNormalizationErrorCode::LoudnessMeasurementFailed:
        {
            return "Loudness measurement failed";
        }
        case AudioNormalizationErrorCode::SilentInputCannotBeNormalized:
        {
            return "Input audio is effectively silent and cannot be normalized";
        }
        case AudioNormalizationErrorCode::OutputRenderFailed:
        {
            return "Could not render normalized audio";
        }
        case AudioNormalizationErrorCode::OutputValidationFailed:
        {
            return "Could not validate normalized audio output";
        }
        case AudioNormalizationErrorCode::TemporaryOutputCleanupFailed:
        {
            return "Could not remove temporary normalization output";
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

// Public boundary: composes the analyzer scan and fingerprint pass into a single analysis result.
std::expected<common::core::AudioLoudnessAnalysis, AudioNormalizationError> measureAudioLoudness(
    const std::filesystem::path& input)
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
    registerBasicAudioFormats(format_manager);
    auto reader = openAudioReader(format_manager, input);
    if (reader == nullptr)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::CouldNotOpenInput,
        }};
    }

    AudioNormalizationErrorCode error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
    std::string error_message;
    auto measurement = runLoudnessAnalyzer(*reader, error_code, error_message);
    if (!measurement.has_value())
    {
        return std::unexpected{AudioNormalizationError{error_code, std::move(error_message)}};
    }

    auto fingerprint = fingerprintAudioFile(input, error_message);
    if (!fingerprint.has_value())
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::LoudnessMeasurementFailed,
            std::move(error_message),
        }};
    }

    return common::core::AudioLoudnessAnalysis{
        .measurement = *measurement,
        .fingerprint = *std::move(fingerprint),
        .analyzer_id = g_analyzer_id,
        .analyzer_version = libebur128Version(),
    };
}

// Public boundary: renders a gain-normalized WAV copy of the supplied input file.
std::expected<AudioNormalizationOutcome, AudioNormalizationError> normalizeAudioFile(
    const std::filesystem::path& input, const std::filesystem::path& output,
    const common::core::AudioNormalizationTarget& target)
{
    if (input.empty())
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::InputFileMissing,
        }};
    }
    if (output.empty())
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::OutputPathRequired,
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
    registerBasicAudioFormats(format_manager);
    auto reader = openAudioReader(format_manager, input);
    if (reader == nullptr)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::CouldNotOpenInput,
        }};
    }

    AudioNormalizationErrorCode error_code = AudioNormalizationErrorCode::LoudnessMeasurementFailed;
    std::string error_message;
    auto source_measurement = runLoudnessAnalyzer(*reader, error_code, error_message);
    if (!source_measurement.has_value())
    {
        return std::unexpected{AudioNormalizationError{error_code, std::move(error_message)}};
    }

    // Inputs below the silence threshold (or with non-finite measured loudness) carry too little
    // signal energy for gain-only normalization to produce a meaningful result.
    if (!std::isfinite(source_measurement->integrated_loudness_lufs) ||
        source_measurement->integrated_loudness_lufs < g_silent_loudness_threshold_lufs)
    {
        return std::unexpected{AudioNormalizationError{
            AudioNormalizationErrorCode::SilentInputCannotBeNormalized,
        }};
    }

    const NormalizationGain gain = calculateNormalizationGainDb(*source_measurement, target);
    const double gain_linear = std::pow(10.0, gain.gain_db / 20.0);

    const std::filesystem::path temporary_output = temporaryOutputPath(output);
    if (!renderNormalizedAudioFile(
            *reader, temporary_output, gain_linear, error_code, error_message))
    {
        removePartialOutputOnFailure(temporary_output);
        return std::unexpected{AudioNormalizationError{error_code, std::move(error_message)}};
    }

    auto rendered_analysis = validateRenderedAudioFile(temporary_output, error_code, error_message);
    if (!rendered_analysis.has_value())
    {
        removePartialOutputOnFailure(temporary_output);
        return std::unexpected{AudioNormalizationError{error_code, std::move(error_message)}};
    }

    // Replace the final output path atomically when possible. std::filesystem::rename overwrites
    // existing files on POSIX and on Windows when both paths resolve to the same volume.
    std::filesystem::rename(temporary_output, output, filesystem_error);
    if (filesystem_error)
    {
        // Fall back to remove + copy when rename across volumes (or other rare cases) fails.
        std::error_code remove_error;
        std::filesystem::remove(output, remove_error);
        std::filesystem::rename(temporary_output, output, filesystem_error);
        if (filesystem_error)
        {
            removePartialOutputOnFailure(temporary_output);
            return std::unexpected{AudioNormalizationError{
                AudioNormalizationErrorCode::OutputRenderFailed,
                "Could not move normalized output into place: " + filesystem_error.message(),
            }};
        }
    }

    return AudioNormalizationOutcome{
        .metadata =
            common::core::AudioLoudnessMetadata{
                .target = target,
                .analysis = *std::move(rendered_analysis),
            },
        .source_measurement = *source_measurement,
        .applied_gain_db = gain.gain_db,
        .limited_by_peak_ceiling = gain.limited_by_peak_ceiling,
    };
}

} // namespace rock_hero::common::audio
