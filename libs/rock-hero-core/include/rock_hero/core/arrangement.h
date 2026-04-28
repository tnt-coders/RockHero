/*!
\file arrangement.h
\brief Arrangement entity within a Chart: a part/rated-difficulty variant with its note events.
*/

#pragma once

#include <cstdint>
#include <rock_hero/core/difficulty.h>
#include <rock_hero/core/timeline.h>
#include <vector>

namespace rock_hero::core
{

/*! \brief A single note to be played by the player. */
struct NoteEvent
{
    /*! \brief Note position on the song timeline. */
    TimePosition position;

    /*! \brief Sustain duration; zero means a non-sustained note. */
    TimeDuration duration;

    /*! \brief Guitar string number from 1 to 6; one is high E and six is low E. */
    int string_number{0};

    /*! \brief Fret number; zero means an open string. */
    int fret{0};
};

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
\brief One playable variant of a Chart, identified by part and numeric difficulty.

An Arrangement owns the full sequence of NoteEvents the player must execute. The song library
treats these as plain data; scoring logic lives in rock-hero-game.
*/
struct Arrangement
{
    /*! \brief Guitar part played by this arrangement. */
    Part part{Part::Lead};

    /*! \brief Numeric difficulty rating represented by this arrangement. */
    DifficultyRating difficulty;

    /*! \brief Ordered note events the player must execute for this arrangement. */
    std::vector<NoteEvent> note_events;
};

} // namespace rock_hero::core
