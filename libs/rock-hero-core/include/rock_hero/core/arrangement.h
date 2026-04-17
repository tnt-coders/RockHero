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

/*!
\brief A single note to be played by the player.
*/
struct NoteEvent
{
    // Onset time from the start of the song.
    double time_seconds{0.0};

    // Sustain duration. Zero means a non-sustained note.
    double duration_seconds{0.0};

    // Guitar string number from 1 to 6. One is high E and six is low E.
    int string_number{0};

    // Fret number. Zero means an open string.
    int fret{0};
};

/*!
\brief Guitar part within an arrangement.
*/
enum class Part : std::uint8_t
{
    Lead,
    Rhythm,
    Bass
};

/*!
\brief Difficulty tier for an arrangement.
*/
enum class Difficulty : std::uint8_t
{
    Easy,
    Medium,
    Hard,
    Expert
};

/*!
\brief One playable variant of a Chart, identified by part and difficulty.

An Arrangement owns the full sequence of NoteEvents the player must execute. The song library
treats these as plain data; scoring logic lives in rock-hero-game.
*/
struct Arrangement
{
    Part part{Part::Lead};
    Difficulty difficulty{Difficulty::Expert};
    std::vector<NoteEvent> note_events;
};

} // namespace rock_hero::core
