/** @file Arrangement.h
    @brief Arrangement entity within a Chart: a part/difficulty variant with its note events.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rock_hero
{

/** A single note to be played by the player. */
struct NoteEvent
{
    double time_seconds{0.0};     ///< Onset time from the start of the song.
    double duration_seconds{0.0}; ///< Sustain duration (0 for non-sustained notes).
    int string_number{0};         ///< Guitar string 1–6 (1 = high e, 6 = low E).
    int fret{0};                  ///< Fret number (0 = open string).
};

/** Guitar part within an arrangement. */
enum class Part : std::uint8_t
{
    Lead,
    Rhythm,
    Bass
};

/** Difficulty tier for an arrangement. */
enum class Difficulty : std::uint8_t
{
    Easy,
    Medium,
    Hard,
    Expert
};

/** One playable variant of a Chart, identified by part and difficulty.

    An Arrangement owns the full sequence of NoteEvents the player must
    execute. The song library treats these as plain data; scoring logic
    lives in rock-hero-game.
*/
struct Arrangement
{
    Part part{Part::Lead};
    Difficulty difficulty{Difficulty::Expert};
    std::vector<NoteEvent> note_events;
};

} // namespace rock_hero
