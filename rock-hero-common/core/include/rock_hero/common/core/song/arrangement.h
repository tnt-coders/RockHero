/*!
\file arrangement.h
\brief Arrangement entity: one playable route with audio and tone automation.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/song/difficulty.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Guitar part within an arrangement. */
enum class Part : std::uint8_t
{
    /*! \brief Lead guitar part, typically melodies and solos. */
    Lead,
    /*! \brief Rhythm guitar part, typically chords and riffs. */
    Rhythm,
    /*! \brief Bass guitar part. */
    Bass
};

/*!
\brief One playable route, identified by part and numeric difficulty.

An Arrangement owns the playable data for one path through a song: the backing audio selected for
that path and the package-relative tone document used by the audio adapter. Chart, tuning, and
note-event storage are intentionally deferred until note display or gameplay needs the model.
*/
struct Arrangement
{
    /*! \brief Stable arrangement identifier used by project editor state. */
    std::string id;

    /*! \brief Guitar part played by this arrangement. */
    Part part{Part::Lead};

    /*! \brief Numeric difficulty rating represented by this arrangement. */
    DifficultyRating difficulty;

    /*! \brief Backing audio assigned to this arrangement. */
    AudioAsset audio_asset;

    /*! \brief Full natural duration of the assigned backing audio. */
    TimeDuration audio_duration;

    /*!
    \brief Package-relative tone document interpreted by common/audio.

    Interpreted exclusively by common/audio; core validates and persists the reference but treats
    the target document as opaque audio-owned data.
    */
    std::string tone_document_ref;

    /*!
    \brief Authored tone schedule for this arrangement.

    Empty until the user authors tone regions; loading synthesizes a runtime-only default region
    from tone_document_ref instead of storing one here, so untouched projects never gain a
    persisted tone track on re-save.
    */
    ToneTrack tone_track;

    /*!
    \brief Package-relative chart document reference (`charts/<uuid>.chart.json`), or empty.

    The chart file is the authoritative persisted form; saves validate its presence but do not
    rewrite it until chart editing exists.
    */
    std::string chart_ref;

    /*!
    \brief Chart content loaded from chart_ref at package read, when a reference exists.

    Runtime convenience for display and gameplay consumers; not compared field-by-field against
    the file on save because the file remains authoritative while charts are read-only.
    */
    std::optional<Chart> chart;

    /*!
    \brief Calculates the range the arrangement audio occupies on the session timeline.

    Starts at the asset's start offset, so a backing recording whose content begins after the
    song's first beat (a positive offset) sits later on the timeline with silence before it.

    \return Timeline range from the audio start offset through the offset plus the audio duration.
    */
    [[nodiscard]] constexpr TimeRange audioTimelineRange() const noexcept
    {
        return TimeRange{
            .start = TimePosition{audio_asset.start_offset.seconds},
            .end = TimePosition{audio_asset.start_offset.seconds + audio_duration.seconds},
        };
    }

    /*!
    \brief Compares two arrangements by their stored fields.
    \param lhs Left-hand arrangement.
    \param rhs Right-hand arrangement.
    \return True when both arrangements store equal values.
    */
    friend bool operator==(const Arrangement& lhs, const Arrangement& rhs) = default;
};

} // namespace rock_hero::common::core
