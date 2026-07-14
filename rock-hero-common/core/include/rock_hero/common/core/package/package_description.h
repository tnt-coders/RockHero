/*!
\file package_description.h
\brief Metadata-only peek into a native song package, without extraction.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Tuning summary peeked from one arrangement's chart document. */
struct ArrangementTuningDescription
{
    /*! \brief Open-string pitches from the lowest-pitched string upward, such as "E2". */
    std::vector<std::string> strings;

    /*! \brief Capo fret; zero means no capo. */
    int capo{0};

    /*! \brief Fine tuning offset in cents. */
    double cent_offset{0.0};
};

/*! \brief One arrangement's description as peeked from song.json and its chart entry. */
struct ArrangementDescription
{
    /*! \brief Stable arrangement id from song.json. */
    std::string id;

    /*! \brief Playable part; absent when the persisted token was unsupported (warned). */
    std::optional<Part> part;

    /*! \brief Package-relative chart document reference; empty when the arrangement has none. */
    std::string chart_ref;

    /*! \brief Tuning peeked from the chart entry; absent without a chart or on a chart warning. */
    std::optional<ArrangementTuningDescription> tuning;

    /*! \brief True when the arrangement's referenced backing audio entry exists in the archive. */
    bool audio_asset_present{false};
};

/*!
\brief Everything the library index needs to describe a package, read without extraction.

Reading is deliberately lenient past the structural gate: a package that IS a readable native
song package with a supported format version always yields a description, and per-entry damage
(a corrupt chart, an unsupported part token, a missing audio entry) lands in `warnings` instead
of failing the read — a broken download should show up in the library as a warned entry, never
as a silently missing song.
*/
struct PackageDescription
{
    /*! \brief song.json format version (1 today; plan 10 owns the tolerance ladder). */
    int format_version{0};

    /*! \brief Descriptive song metadata; blank fields when the package never set them. */
    SongMetadata metadata;

    /*! \brief Per-arrangement descriptions in document order. */
    std::vector<ArrangementDescription> arrangements;

    /*! \brief Non-fatal, human-readable problems found while peeking. */
    std::vector<std::string> warnings;
};

/*!
\brief Peeks a native song package's description straight from the archive.

Streams song.json and each referenced chart entry from the ZIP; never extracts audio, never
writes a workspace, never touches the filesystem beyond reading the archive. The library index
(plan 26) rescans with this instead of extracting, which is what keeps startup instant.

\param package_path Native `.rock` package file.
\return The description, or a typed failure when the file is not a readable supported package.
*/
[[nodiscard]] std::expected<PackageDescription, SongPackageError> readRockSongPackageDescription(
    const std::filesystem::path& package_path);

} // namespace rock_hero::common::core
