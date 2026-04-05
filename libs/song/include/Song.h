/** @file Song.h
    @brief Song aggregate root: the top-level persistence and session unit.
*/

#pragma once

#include "Chart.h"

#include <string>

namespace rock_hero
{

/** Descriptive metadata attached to a Song. */
struct SongMetadata
{
    std::string title;
    std::string artist;
    std::string album;
    int year{0};
};

/** Top-level aggregate root for a Rock Hero song.

    A Song owns all data needed to start a game or editing session:
    - Descriptive metadata (title, artist, etc.)
    - A reference to the audio backing track asset.
    - An opaque reference to the tone automation timeline; this blob is
      interpreted exclusively by audio-engine and is never parsed here.
    - A Chart containing the playable Arrangements.

    song depends only on standard C++. It has no dependency on JUCE
    or Tracktion Engine.
*/
struct Song
{
    SongMetadata metadata;

    /** Path or asset identifier for the audio backing track. */
    std::string audio_asset_ref;

    /** Opaque serialized tone automation data.
        Interpreted exclusively by audio-engine; song-model treats it as a
        pass-through blob.
    */
    std::string tone_timeline_ref;

    Chart chart;
};

} // namespace rock_hero
