/*!
\file rock_song_package_format.h
\brief Shared song-package format rules used by the reader and writer translation units.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <string_view>

namespace rock_hero::common::core
{

/*! \brief Name of the required root song document inside a native song package. */
inline constexpr std::string_view g_song_document_name{"song.json"};

/*!
\brief Fixed decimal precision for persisted anchor seconds.

Anchor seconds are the only absolute time stored in a package, persisted at a fixed three-decimal
(millisecond) grid. This matches the Song Data Model note in docs/design/architecture.md.
*/
inline constexpr int g_timing_decimals = 3;

/*!
\brief Reports whether a package-relative reference stays inside its workspace.
\param path Package-relative path taken from a song document.
\return True when the path is relative and never escapes upward.
*/
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);

/*!
\brief Validates the structural tempo-map rules shared by package read and write.
\param tempo_map Parsed or about-to-be-persisted tempo map.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateTempoMap(const TempoMap& tempo_map);

/*! \brief Parsed form of a `"<measure>:<beat>"` grid position token. */
struct BeatPositionToken
{
    /*! \brief One-based measure on the song grid. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};
};

/*!
\brief Parses a `"<measure>:<beat>"` grid position token.

The one token grammar shared by tempo-map anchors and tone-region endpoints; sub-beat `+` suffixes
are rejected until a format revision introduces them.

\param text Token text from a song document.
\return Parsed position, or empty when the token is malformed.
*/
[[nodiscard]] std::optional<BeatPositionToken> parseBeatPositionToken(const std::string& text);

/*!
\brief Formats a grid position as its `"<measure>:<beat>"` token.
\param measure One-based measure on the song grid.
\param beat One-based beat within the measure.
\return Token text for a song document.
*/
[[nodiscard]] std::string formatBeatPositionToken(int measure, int beat);

/*!
\brief Validates the structural tone-track rules shared by package read and write.

Checks region IDs (canonical, unique), endpoint validity against the tempo map's grid and
terminal anchor, strict start-before-end ordering, ascending non-overlapping regions, and
canonical tone document references. File existence is checked by the caller because read and
write resolve documents against different directories.

\param tone_track Parsed or about-to-be-persisted tone track.
\param tempo_map Tempo map the region endpoints must address.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateToneTrack(
    const ToneTrack& tone_track, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
