/*!
\file library_index.h
\brief Value model for the game's cached song-library index.

The index is game-owned per-user cache data derived from read-only packages: it is always safe to
discard and rebuild by rescanning, so nothing in it is authoritative. Entries are keyed by
absolute package path until docs/plans/roadmap/10-format-versioning-and-chart-identity.md supplies a
package identity hash for move detection.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/package/package_description.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*! \brief One arrangement's difficulty intensity as computed by a versioned calculator. */
struct ArrangementIntensity
{
    /*! \brief Calculator output on the display intensity scale. */
    double value{0.0};

    /*! \brief Version of the calculator that produced the value; bumping it forces recompute. */
    int calculator_version{0};
};

/*! \brief One arrangement's library-facing summary as peeked from its package. */
struct LibraryArrangementSummary
{
    /*! \brief Persisted arrangement identity from song.json; empty when the entry lacked one. */
    std::string id;

    /*! \brief Arrangement part; empty when the package carried an unsupported part token. */
    std::optional<common::core::Part> part;

    /*! \brief Tuning summary from the chart entry; empty when the chart could not be peeked. */
    std::optional<common::core::ArrangementTuningDescription> tuning;

    /*!
    \brief Derived intensity; empty means the "Unknown" difficulty bucket.

    Stays empty until docs/plans/roadmap/11-derived-difficulty-calculator.md ships a calculator.
    */
    std::optional<ArrangementIntensity> intensity;
};

/*! \brief One scanned package's cached library entry. */
struct LibraryEntry
{
    /*! \brief Absolute package path; the entry key until a package identity hash exists. */
    std::filesystem::path package_path;

    /*! \brief Package file size in bytes at scan time, for change detection. */
    std::int64_t file_size_bytes{0};

    /*! \brief Package modification time in milliseconds since the epoch, for change detection. */
    std::int64_t modification_time_milliseconds{0};

    /*!
    \brief Package identity hash; empty until
           docs/plans/roadmap/10-format-versioning-and-chart-identity.md lands (enables move detection
           and stable album-art identity).
    */
    std::string package_hash;

    /*! \brief Song metadata peeked from song.json. */
    common::core::SongMetadata metadata;

    /*! \brief Per-arrangement summaries in package order. */
    std::vector<LibraryArrangementSummary> arrangements;

    /*!
    \brief Cached album-art image file name beside the index; empty until
           docs/plans/roadmap/43-song-information-and-art.md adds album art.
    */
    std::string album_art_file_name;

    /*! \brief Non-fatal scan warnings; a non-empty list marks the entry's Warning scan status. */
    std::vector<std::string> warnings;

    /*! \brief True when the scan recorded warnings for this package. */
    [[nodiscard]] bool hasWarnings() const noexcept
    {
        return !warnings.empty();
    }
};

/*! \brief The game's whole cached song library. */
struct LibraryIndex
{
    /*! \brief Cached entries, one per known package. */
    std::vector<LibraryEntry> entries;
};

} // namespace rock_hero::game::core
