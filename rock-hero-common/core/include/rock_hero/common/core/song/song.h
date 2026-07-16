/*!
\file song.h
\brief Song aggregate root: the top-level persistence and session unit.
*/

#pragma once

#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Navigation/practice marker naming the song passage that starts at a position.

Song-level because sections describe the song's structure, not one arrangement's tab: every
arrangement (and the 3D highway) shares the same section list.
*/
struct SongSection
{
    /*! \brief Musical position the section starts at. */
    GridPosition position;

    /*! \brief Free-form section name, such as "verse" or "chorus", taken verbatim from import. */
    std::string name;

    /*!
    \brief Compares two sections by their stored fields.
    \param lhs Left-hand section.
    \param rhs Right-hand section.
    \return True when both sections store equal values.
    */
    friend bool operator==(const SongSection& lhs, const SongSection& rhs) = default;
};

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

    /*! \brief Song-structure section markers, sorted by position. */
    std::vector<SongSection> sections;

    /*! \brief Playable part/difficulty-rating variants available for the song. */
    std::vector<Arrangement> arrangements;
};

} // namespace rock_hero::common::core
