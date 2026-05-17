/*!
\file song.h
\brief Song aggregate root: the top-level persistence and session unit.
*/

#pragma once

#include <rock_hero/common/core/arrangement.h>
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
- Playable Arrangements for authored parts.

Arrangement-owned tone document references are interpreted exclusively by common/audio and the
referenced documents are never parsed here.
Song depends only on standard C++. It has no dependency on JUCE or Tracktion Engine.
*/
struct Song
{
    /*! \brief Descriptive metadata for the song. */
    SongMetadata metadata;

    /*! \brief Playable part/difficulty-rating variants available for the song. */
    std::vector<Arrangement> arrangements;
};

} // namespace rock_hero::common::core
