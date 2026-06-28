/*!
\file chart_event.h
\brief A single timed playable event: a single note or a chord instance.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/grid_position.h>
#include <string>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Articulation flags and values applied to a struck note or string. */
struct Techniques
{
    /*! \brief Whether the note is played with vibrato. */
    bool vibrato{false};

    /*! \brief Whether the note is palm-muted. */
    bool palm_mute{false};

    /*! \brief Bend amount in whole steps as an exact fraction; absent means no bend. */
    std::optional<Fraction> bend;

    /*!
    \brief Reports whether any technique is active.
    \return True when at least one technique flag or value is set.
    */
    [[nodiscard]] bool any() const noexcept
    {
        return vibrato || palm_mute || bend.has_value();
    }

    /*!
    \brief Compares two technique sets by their stored fields.
    \param lhs Left-hand technique set.
    \param rhs Right-hand technique set.
    \return True when both store equal flags and bend value.
    */
    friend bool operator==(const Techniques& lhs, const Techniques& rhs) = default;
};

/*! \brief A single-string note played at the event onset. */
struct SingleNote
{
    /*! \brief One-based string number; one is the highest-pitched string. */
    int string_number{0};

    /*! \brief Fret number; zero means an open string. */
    int fret{0};

    /*! \brief Articulations applied to the note. */
    Techniques techniques;

    /*!
    \brief Compares two single notes by their stored fields.
    \param lhs Left-hand single note.
    \param rhs Right-hand single note.
    \return True when both store equal string, fret, and techniques.
    */
    friend bool operator==(const SingleNote& lhs, const SingleNote& rhs) = default;
};

/*! \brief One string of a chord whose playback deviates from the chord-wide defaults. */
struct ChordStringDeviation
{
    /*! \brief One-based string of the referenced template whose playback deviates. */
    int string_number{0};

    /*! \brief Sustain end for this string, overriding the event end; absent keeps the event end. */
    std::optional<GridPosition> end;

    /*! \brief Articulations applied to this string. */
    Techniques techniques;

    /*!
    \brief Compares two string deviations by their stored fields.
    \param lhs Left-hand string deviation.
    \param rhs Right-hand string deviation.
    \return True when both store equal string, end, and techniques.
    */
    friend bool operator==(const ChordStringDeviation& lhs, const ChordStringDeviation& rhs) =
        default;
};

/*! \brief A chord played at the event onset by reference to a chord template. */
struct ChordInstance
{
    /*! \brief Id of the ChordTemplate this event plays. */
    std::string template_id;

    /*! \brief Per-string deviations from the chord-wide end and articulations. */
    std::vector<ChordStringDeviation> string_deviations;

    /*!
    \brief Compares two chord instances by their stored fields.
    \param lhs Left-hand chord instance.
    \param rhs Right-hand chord instance.
    \return True when both store equal template id and string deviations.
    */
    friend bool operator==(const ChordInstance& lhs, const ChordInstance& rhs) = default;
};

/*!
\brief One timed playable event positioned on the beat grid.

A ChartEvent is the unit of the arrangement chart: one onset the player executes, which is either a
single note or a chord. start and the optional end are grid positions resolved to seconds through the
song TempoMap; an absent end means the event is non-sustained. The content variant makes the
note/chord distinction explicit, so a single note can never carry chord data and a chord can never
carry an inline fret.
*/
struct ChartEvent
{
    /*! \brief Onset position on the beat grid. */
    GridPosition start;

    /*! \brief Sustain end position; absent means non-sustained. */
    std::optional<GridPosition> end;

    /*! \brief Whether this event is a single note or a chord. */
    std::variant<SingleNote, ChordInstance> content;

    /*!
    \brief Compares two chart events by their stored fields.
    \param lhs Left-hand chart event.
    \param rhs Right-hand chart event.
    \return True when both store equal start, end, and content.
    */
    friend bool operator==(const ChartEvent& lhs, const ChartEvent& rhs) = default;
};

} // namespace rock_hero::common::core
