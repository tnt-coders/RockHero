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
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <string_view>
#include <vector>

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
\brief Returns the stable song-document token for a guitar part.

The one canonical wire spelling of \ref Part, shared by song.json read and write and the game
library index so every persisted surface speaks an identical vocabulary. A new enumerator is
encoded here once and every serializer follows, instead of each site hand-maintaining its own
token table.

\param part Guitar part to encode.
\return The part's persisted token: `"Lead"`, `"Rhythm"`, or `"Bass"`.
*/
[[nodiscard]] std::string_view partToken(Part part) noexcept;

/*!
\brief Parses a song-document part token back into a \ref Part.
\param token Persisted part token.
\return The decoded part, or std::nullopt for an unrecognized token.
*/
[[nodiscard]] std::optional<Part> parsePartToken(std::string_view token) noexcept;

/*!
\brief One playable route, identified by part and numeric difficulty.

An Arrangement owns the playable data for one path through a song: the backing audio selected for
that path and the named tones its tone track schedules. Chart, tuning, and note-event storage are
intentionally deferred until note display or gameplay needs the model.
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
    \brief Named tones this arrangement can use, keyed by their tone document reference.

    The catalog owns tone names; tone_track regions reference a tone by its document ref, and
    loading guarantees every referenced tone has a catalog entry. The tone document itself is
    interpreted exclusively by common/audio; core treats it as opaque audio-owned data addressed
    by the canonical `tones/<uuid>/tone.json` reference.
    */
    std::vector<Tone> tones;

    /*!
    \brief Authored tone schedule for this arrangement.

    Loading normalizes every tone-bearing arrangement to explicit gap-free regions (a tone-less
    arrangement gets a minted default tone plus a whole-song region as it enters the editor), so
    the editor splits, selects, and deletes regions uniformly with no synthesized default case.
    */
    ToneTrack tone_track;

    /*!
    \brief Plugin-parameter automation curves for this arrangement's tone chains.

    Musical positions are the source of truth; the audio layer's seconds curves are derived caches
    rebuilt from these entries. Keyed by durable minted plugin id + parameter id, so entries follow
    a plugin through chain reorder and survive remove (an unresolved entry renders disabled).
    */
    std::vector<ToneParameterAutomation> tone_automation;

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

/*!
\brief Looks up a tone's user-facing name in an arrangement's catalog by its document reference.

Regions carry no name of their own; their label comes from the tone they reference. Returns an
empty string when the reference is not in the catalog, so callers can apply their own fallback.

\param arrangement Arrangement whose tone catalog is searched.
\param tone_document_ref Package-relative tone document reference to look up.
\return The catalog tone's name, or an empty string when it is not present.
*/
[[nodiscard]] inline std::string toneNameFor(
    const Arrangement& arrangement, const std::string& tone_document_ref)
{
    for (const Tone& tone : arrangement.tones)
    {
        if (tone.tone_document_ref == tone_document_ref)
        {
            return tone.name;
        }
    }
    return {};
}

} // namespace rock_hero::common::core
