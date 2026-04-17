/*!
\file song.h
\brief Song aggregate root: the top-level persistence and session unit.
*/

#pragma once

#include <rock_hero/core/chart.h>

#include <string>

namespace rock_hero::core
{

/*!
\brief Descriptive metadata attached to a Song.
*/
struct SongMetadata
{
    std::string title;
    std::string artist;
    std::string album;
    int year{0};
};

/*!
\brief Top-level aggregate root for a Rock Hero song.

A Song owns all data needed to start a game or editing session:
- Descriptive metadata such as title, artist, and album.
- A reference to the audio backing track asset.
- An opaque reference to the tone automation timeline.
- A Chart containing the playable Arrangements.

The tone automation blob is interpreted exclusively by rock-hero-audio and is never parsed here.
Song depends only on standard C++. It has no dependency on JUCE or Tracktion Engine.
*/
struct Song
{
    SongMetadata metadata;

    /*!
\brief Path or asset identifier for the audio backing track.
*/
    std::string audio_asset_ref;

    /*!
\brief Opaque serialized tone automation data.

Interpreted exclusively by rock-hero-audio; song-model treats it as a pass-through blob.
*/
    std::string tone_timeline_ref;

    Chart chart;
};

} // namespace rock_hero::core
