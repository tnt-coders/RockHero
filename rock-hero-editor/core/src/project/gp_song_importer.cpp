#include "project/gp_song_importer.h"

#include "project/gp_chart_builder.h"
#include "project/gp_score_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <expected>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/song/audio_transcode.h>
#include <rock_hero/common/core/chart/chart_document.h>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// The score document every .gp container carries.
constexpr const char* g_score_entry{"Content/score.gpif"};

// Reads one zip entry fully into a memory block; empty optional when the entry is missing.
[[nodiscard]] std::optional<juce::MemoryBlock> readZipEntry(
    juce::ZipFile& archive, const juce::String& entry_name)
{
    const int entry_index = archive.getIndexOfFileName(entry_name);
    if (entry_index < 0)
    {
        return std::nullopt;
    }

    const std::unique_ptr<juce::InputStream> stream{archive.createStreamForEntry(entry_index)};
    if (stream == nullptr)
    {
        return std::nullopt;
    }

    juce::MemoryBlock contents;
    stream->readIntoMemoryBlock(contents);
    return contents;
}

} // namespace

// Converts a Guitar Pro file into a workspace song: charts from the score, the tempo map from
// its audio sync points, and the embedded backing audio copied beside them.
std::expected<common::core::Song, SongImportError> GpSongImporter::importSong(
    const std::filesystem::path& source_path, const std::filesystem::path& workspace_directory)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(source_path, filesystem_error))
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::MissingSource,
            "Guitar Pro file does not exist: " + source_path.string(),
        }};
    }

    juce::ZipFile archive{common::core::juceFileFromPath(source_path)};
    const auto score_text = readZipEntry(archive, g_score_entry);
    if (!score_text.has_value())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::ExtractionFailed,
            "Guitar Pro file has no score document: " + source_path.string(),
        }};
    }

    auto score = parseGpScore(score_text->toString().toStdString());
    if (!score.has_value())
    {
        return std::unexpected{SongImportError{
            score.error().code,
            "Could not read the Guitar Pro score: " + std::move(score.error().message),
        }};
    }

    if (score->embedded_audio_entry.empty())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::InvalidImportedSong,
            "Guitar Pro file has no embedded backing audio; arrangements need backing audio",
        }};
    }

    auto built = buildGpSong(*score);
    if (!built.has_value())
    {
        return std::unexpected{SongImportError{
            built.error().code,
            "Could not convert the Guitar Pro score: " + std::move(built.error().message),
        }};
    }

    // Bring the embedded backing audio into the workspace as FLAC, RockHero's canonical package
    // format: lossless, compact, and decoded identically for playback and the waveform thumbnail
    // (lossy sources decode differently for each, so their waveforms drift). An already-FLAC source
    // is copied; anything else is transcoded.
    const auto audio_bytes = readZipEntry(archive, juce::String{score->embedded_audio_entry});
    if (!audio_bytes.has_value())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::ExtractionFailed,
            "Could not extract the embedded backing audio: " + score->embedded_audio_entry,
        }};
    }
    const std::filesystem::path audio_relative = std::filesystem::path{"audio"} / "backing.flac";
    const juce::File flac_file =
        common::core::juceFileFromPath(workspace_directory / audio_relative);
    if (!flac_file.getParentDirectory().createDirectory())
    {
        return std::unexpected{SongImportError{
            SongImportErrorCode::ExtractionFailed,
            "Could not create the workspace audio directory",
        }};
    }

    std::string source_extension =
        std::filesystem::path{score->embedded_audio_entry}.extension().string();
    std::ranges::transform(source_extension, source_extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (source_extension == ".flac")
    {
        if (!flac_file.replaceWithData(audio_bytes->getData(), audio_bytes->getSize()))
        {
            return std::unexpected{SongImportError{
                SongImportErrorCode::ExtractionFailed,
                "Could not write the backing audio into the workspace",
            }};
        }
    }
    else
    {
        // Stage the source beside the target, transcode it to FLAC, then drop the staged copy.
        const std::filesystem::path staged_source_path = workspace_directory /
                                                         std::filesystem::path{"audio"} /
                                                         ("backing_source" + source_extension);
        const juce::File staged_source_file = common::core::juceFileFromPath(staged_source_path);
        if (!staged_source_file.replaceWithData(audio_bytes->getData(), audio_bytes->getSize()))
        {
            return std::unexpected{SongImportError{
                SongImportErrorCode::ExtractionFailed,
                "Could not write the backing audio into the workspace",
            }};
        }

        const auto transcoded = common::audio::transcodeToFlac(
            staged_source_path, workspace_directory / audio_relative);
        staged_source_file.deleteFile();
        if (!transcoded.has_value())
        {
            return std::unexpected{SongImportError{
                SongImportErrorCode::InvalidImportedSong,
                "Could not convert the backing audio to FLAC: " + transcoded.error().message,
            }};
        }
    }

    // Positive frame padding is silence Guitar Pro places before the audio when the recording's
    // content starts after the score's first beat; it becomes the backing audio's start offset so
    // the notes line up. Guitar Pro counts it in fixed 44.1kHz frames regardless of the asset's
    // real sample rate (verified against 48kHz corpus assets whose sync tempos only reproduce at
    // 44100). Negative padding is cosmetic leading silence that does not shift note timing. The
    // offset is snapped to the same millisecond grid as the tempo map so audio and notes stay on
    // one time base and the package stores it cleanly.
    constexpr double g_gp_frame_rate{44100.0};
    const double raw_offset_seconds = std::max(0, score->frame_padding) / g_gp_frame_rate;
    const common::core::TimeDuration audio_start_offset{
        static_cast<double>(std::llround(raw_offset_seconds * 1000.0)) / 1000.0
    };

    // Materialize the song: one arrangement per built track, sharing the backing audio, each
    // with its chart document written beside the audio.
    common::core::Song song;
    song.metadata = std::move(built->metadata);
    song.tempo_map = std::move(built->tempo_map);
    song.sections = std::move(built->sections);
    for (GpBuiltArrangement& arrangement : built->arrangements)
    {
        const std::string id = common::core::generatePackageId();
        const std::string chart_ref = "charts/" + id + ".chart.json";
        if (auto written = common::core::writeChartDocument(
                workspace_directory / std::filesystem::path{chart_ref}, arrangement.chart);
            !written.has_value())
        {
            return std::unexpected{SongImportError{
                SongImportErrorCode::InvalidImportedSong,
                "Could not write an imported chart: " + std::move(written.error().message),
            }};
        }

        song.arrangements.push_back(
            common::core::Arrangement{
                .id = id,
                .part = arrangement.part,
                .difficulty = common::core::DifficultyRating{},
                .audio_asset =
                    common::core::AudioAsset{
                        .path = audio_relative,
                        .normalization = std::nullopt,
                        .start_offset = audio_start_offset,
                    },
                .audio_duration = common::core::TimeDuration{},
                .tones = {},
                .tone_track = {},
                .tone_automation = {},
                .chart_ref = chart_ref,
                .chart = std::move(arrangement.chart),
            });
    }

    // Conversion notes are diagnostics, not failures: log them so importer behavior stays
    // observable without blocking the import.
    for (const std::string& note : built->notes)
    {
        RH_LOG_INFO("editor.import", "gp import: {}", note);
    }

    return song;
}

} // namespace rock_hero::editor::core
