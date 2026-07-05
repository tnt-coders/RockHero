/*!
\file configurable_song_audio.h
\brief Configurable song-audio test implementation.
*/

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/song/i_song_audio.h>

namespace rock_hero::common::audio::testing
{

/*!
\brief ISongAudio implementation with configurable preparation and activation outcomes.

Use this when tests need to exercise project loading, active-arrangement replacement, or failure
paths without constructing the concrete audio engine. The public fields are intentionally exposed
so tests can configure only the behavior they need and assert recorded calls directly.
*/
class ConfigurableSongAudio final : public ISongAudio
{
public:
    /*!
    \brief Records preparation and fills arrangement durations on success.
    \param song Candidate song prepared by the object under test.
    \return Empty success, the next configured typed error, or a configured preparation failure.
    */
    [[nodiscard]] std::expected<void, SongAudioError> prepareSong(common::core::Song& song) override
    {
        prepare_song_call_count += 1;
        if (next_prepare_error.has_value())
        {
            SongAudioError error = std::move(*next_prepare_error);
            next_prepare_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!next_prepare_result)
        {
            return std::unexpected{SongAudioError{
                SongAudioErrorCode::UnreadableAudioFile,
                "Configured song preparation failure",
            }};
        }

        for (common::core::Arrangement& arrangement : song.arrangements)
        {
            last_prepared_audio_asset = arrangement.audio_asset;
            prepared_audio_asset_count += 1;
            if (!failed_prepare_audio_path.empty() &&
                arrangement.audio_asset.path == failed_prepare_audio_path)
            {
                return std::unexpected{SongAudioError{
                    SongAudioErrorCode::UnreadableAudioFile,
                    "Configured song preparation failure for: " +
                        failed_prepare_audio_path.string(),
                }};
            }

            arrangement.audio_duration = next_prepared_audio_duration;
        }

        return {};
    }

    /*!
    \brief Records the active arrangement and returns the configured activation outcome.
    \param arrangement Arrangement selected by the object under test.
    \return Empty success, the next configured typed error, or a configured activation failure.
    */
    [[nodiscard]] std::expected<void, SongAudioError> setActiveArrangement(
        const common::core::Arrangement& arrangement) override
    {
        last_active_audio_asset = arrangement.audio_asset;
        set_active_arrangement_call_count += 1;
        if (during_active_arrangement_action)
        {
            during_active_arrangement_action();
        }

        if (next_set_active_arrangement_error.has_value())
        {
            SongAudioError error = std::move(*next_set_active_arrangement_error);
            next_set_active_arrangement_error.reset();
            return std::unexpected{std::move(error)};
        }

        if (!next_set_active_arrangement_result)
        {
            return std::unexpected{SongAudioError{
                SongAudioErrorCode::BackendClipInsertionFailed,
                "Configured active-arrangement failure",
            }};
        }

        return {};
    }

    /*!
    \brief Records that the current active arrangement should be cleared.
    \return Empty success or the next configured typed error.
    */
    [[nodiscard]] std::expected<void, SongAudioError> clearActiveArrangement() override
    {
        last_active_audio_asset.reset();
        clear_active_arrangement_call_count += 1;
        if (next_clear_active_arrangement_error.has_value())
        {
            SongAudioError error = std::move(*next_clear_active_arrangement_error);
            next_clear_active_arrangement_error.reset();
            return std::unexpected{std::move(error)};
        }

        return {};
    }

    /*! \brief Duration assigned to each arrangement during successful preparation. */
    common::core::TimeDuration next_prepared_audio_duration{common::core::TimeDuration{4.0}};

    /*! \brief Controls whether the next prepareSong() call accepts the song. */
    bool next_prepare_result{true};

    /*! \brief Controls whether the next setActiveArrangement() call accepts the arrangement. */
    bool next_set_active_arrangement_result{true};

    /*! \brief Optional typed preparation error returned instead of success. */
    std::optional<SongAudioError> next_prepare_error{};

    /*! \brief Optional typed activation error returned instead of success. */
    std::optional<SongAudioError> next_set_active_arrangement_error{};

    /*! \brief Optional typed clear error returned instead of success. */
    std::optional<SongAudioError> next_clear_active_arrangement_error{};

    /*! \brief Specific arrangement audio path that should fail during preparation. */
    std::filesystem::path failed_prepare_audio_path{};

    /*! \brief Last arrangement audio asset visited during preparation. */
    std::optional<common::core::AudioAsset> last_prepared_audio_asset{};

    /*! \brief Last active arrangement audio asset selected for backend playback. */
    std::optional<common::core::AudioAsset> last_active_audio_asset{};

    /*! \brief Optional callback fired from setActiveArrangement() before returning. */
    std::function<void()> during_active_arrangement_action{};

    /*! \brief Number of song-preparation calls received. */
    int prepare_song_call_count{0};

    /*! \brief Number of arrangement assets visited during preparation. */
    int prepared_audio_asset_count{0};

    /*! \brief Number of active-arrangement replacement calls received. */
    int set_active_arrangement_call_count{0};

    /*! \brief Number of active-arrangement clear calls received. */
    int clear_active_arrangement_call_count{0};
};

} // namespace rock_hero::common::audio::testing
