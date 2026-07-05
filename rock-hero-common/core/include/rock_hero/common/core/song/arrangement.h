/*!
\file arrangement.h
\brief Arrangement entity: one playable route with audio and tone automation.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/song/difficulty.h>
#include <rock_hero/common/core/timeline/timeline.h>
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
    \brief Calculates the range occupied by the arrangement audio on the session timeline.
    \return Timeline range from zero through the assigned audio duration.
    */
    [[nodiscard]] constexpr TimeRange audioTimelineRange() const noexcept
    {
        return TimeRange{
            .start = TimePosition{},
            .end = TimePosition{audio_duration.seconds},
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
