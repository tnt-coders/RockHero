/*!
\file song.h
\brief Song aggregate root: the top-level persistence and session unit.
*/

#pragma once

#include <rock_hero/common/core/domain/arrangement.h>
#include <rock_hero/common/core/domain/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Descriptive metadata attached to a Song. */
struct SongMetadata
{
    /*! \brief Song title displayed in browsers and editor views. */
    std::string title;

    /*! \brief Primary artist credited for the song. */
    std::string artist;

    /*! \brief Album or release name associated with the song. */
    std::string album;

    /*! \brief Release year; zero means unknown. */
    int year{0};
};

/*!
\brief Top-level aggregate root for a Rock Hero song.

A Song owns all data needed to start a game or editing session:
- Descriptive metadata such as title, artist, and album.
- The song-level tempo map used by tone automation and future chart grid positions.
- Playable Arrangements for authored parts.

Arrangement-owned tone document references are interpreted exclusively by common/audio and the
referenced documents are never parsed here.
Song depends only on standard C++. It has no dependency on JUCE or Tracktion Engine.
*/
struct Song
{
    /*! \brief Descriptive metadata for the song. */
    SongMetadata metadata;

    /*! \brief Song-level beat grid used by tone automation and future chart timing. */
    TempoMap tempo_map;

    /*! \brief Playable part/difficulty-rating variants available for the song. */
    std::vector<Arrangement> arrangements;
};

} // namespace rock_hero::common::core
