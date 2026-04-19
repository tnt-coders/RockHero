/*!
\file arrangement.h
\brief Arrangement entity within a Chart: a part/difficulty variant with its note events.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rock_hero::core
{

/*! \brief A single note to be played by the player. */
struct NoteEvent
{
    /*! \brief Onset time from the start of the song, in seconds. */
    double time_seconds{0.0};

    /*! \brief Sustain duration in seconds; zero means a non-sustained note. */
    double duration_seconds{0.0};

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

/*! \brief Difficulty tier for an arrangement. */
enum class Difficulty : std::uint8_t
{
    /*! \brief Simplified arrangement aimed at beginners. */
    Easy,
    /*! \brief Intermediate arrangement between Easy and Hard. */
    Medium,
    /*! \brief Advanced arrangement demanding solid technique. */
    Hard,
    /*! \brief Full arrangement as originally written, with no simplifications. */
    Expert
};

/*!
\brief One playable variant of a Chart, identified by part and difficulty.

An Arrangement owns the full sequence of NoteEvents the player must execute. The song library
treats these as plain data; scoring logic lives in rock-hero-game.
*/
struct Arrangement
{
    /*! \brief Guitar part played by this arrangement. */
    Part part{Part::Lead};

    /*! \brief Difficulty tier represented by this arrangement. */
    Difficulty difficulty{Difficulty::Expert};

    /*! \brief Ordered note events the player must execute for this arrangement. */
    std::vector<NoteEvent> note_events;
};

} // namespace rock_hero::core
