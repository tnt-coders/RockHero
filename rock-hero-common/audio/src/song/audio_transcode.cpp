#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <rock_hero/common/audio/song/audio_transcode.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// FLAC compression level (0 fastest ... 8 smallest). Level two keeps imports snappy while still
// roughly halving WAV. FLAC is lossless at every level, so this trades only encode time for size.
constexpr int g_flac_compression_index{2};

// FLAC stores 16- or 24-bit integer samples (juce_FlacAudioFormat.cpp getPossibleBitDepths). Match
// the source's declared precision rather than always writing 24-bit: a lossy 16-bit source (Ogg
// Vorbis reports 16-bit) padded to 24-bit roughly doubles the file while storing only decode noise.
// Sources above 16-bit (24-bit masters, or the platform MP3/AAC decoder's wider output) clamp to
// FLAC's 24-bit ceiling, which is lossless for any real integer source and beyond lossy precision.
constexpr int g_flac_low_bit_depth{16};
constexpr int g_flac_high_bit_depth{24};

// Chooses the FLAC bit depth that preserves the reader's declared precision within FLAC's range.
[[nodiscard]] int flacBitDepthForSource(const juce::AudioFormatReader& reader) noexcept
{
    return reader.bitsPerSample > g_flac_low_bit_depth ? g_flac_high_bit_depth
                                                       : g_flac_low_bit_depth;
}

} // namespace

// Decodes the source through JUCE's format manager (WAV/FLAC/Ogg plus the platform decoder for
// MP3 and AAC) and streams it into a lossless FLAC file, so downstream playback and thumbnail
// reads share one decode-exact source. FLAC sources are copied by the caller and never reach here.
std::expected<void, AudioTranscodeError> transcodeToFlac(
    const std::filesystem::path& source, const std::filesystem::path& destination)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(
        common::core::juceFileFromPath(source))};
    if (reader == nullptr)
    {
        return std::unexpected{AudioTranscodeError{
            .code = AudioTranscodeErrorCode::SourceUndecodable,
            .message = "could not decode audio file: " + source.string(),
        }};
    }

    const juce::File destination_file = common::core::juceFileFromPath(destination);
    destination_file.deleteFile();
    auto file_stream = std::make_unique<juce::FileOutputStream>(destination_file);
    if (!file_stream->openedOk())
    {
        return std::unexpected{AudioTranscodeError{
            .code = AudioTranscodeErrorCode::DestinationUnwritable,
            .message = "could not create FLAC file: " + destination.string(),
        }};
    }

    // createWriterFor consumes the stream only on success; a failed call leaves it here to free.
    std::unique_ptr<juce::OutputStream> stream = std::move(file_stream);
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> writer{flac.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}
            .withSampleRate(reader->sampleRate)
            .withNumChannels(static_cast<int>(reader->numChannels))
            .withBitsPerSample(flacBitDepthForSource(*reader))
            .withQualityOptionIndex(g_flac_compression_index))};
    if (writer == nullptr)
    {
        stream.reset();
        destination_file.deleteFile();
        return std::unexpected{AudioTranscodeError{
            .code = AudioTranscodeErrorCode::DestinationUnwritable,
            .message = "could not create FLAC writer for: " + destination.string(),
        }};
    }

    const bool wrote = writer->writeFromAudioReader(*reader, 0, reader->lengthInSamples);
    // Destroy the writer to flush and finalize the FLAC stream before judging the result.
    writer.reset();
    if (!wrote)
    {
        destination_file.deleteFile();
        return std::unexpected{AudioTranscodeError{
            .code = AudioTranscodeErrorCode::DestinationUnwritable,
            .message = "could not encode FLAC audio into: " + destination.string(),
        }};
    }

    return {};
}

} // namespace rock_hero::common::audio
