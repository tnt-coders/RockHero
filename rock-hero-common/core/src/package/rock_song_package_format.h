/*!
\file rock_song_package_format.h
\brief Shared song-package format rules used by the reader and writer translation units.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Name of the required root song document inside a native song package. */
inline constexpr std::string_view g_song_document_name{"song.json"};

/*!
\brief Reports whether an audio path names a FLAC file, RockHero's only package audio format.
\param path Package-relative or source audio path.
\return True when the extension is `.flac`, compared case-insensitively.
*/
[[nodiscard]] bool hasFlacExtension(const std::filesystem::path& path);

/*!
\brief Validates the structural tempo-map rules shared by package read and write.
\param tempo_map Parsed or about-to-be-persisted tempo map.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateTempoMap(const TempoMap& tempo_map);

/*!
\brief Validates the structural tone-track rules shared by package read and write.

Translates \ref validateToneTrackRules (canonical unique IDs, valid starts on the tempo map's grid,
whole-song coverage, strictly ascending starts, canonical tone document references) into the
package error surface. File existence is checked by the caller because read and write resolve
documents against different directories.

\param tone_track Parsed or about-to-be-persisted tone track.
\param tempo_map Tempo map the region endpoints must address.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateToneTrack(
    const ToneTrack& tone_track, const TempoMap& tempo_map);

/*!
\brief Validates an arrangement's plugin-parameter automation, shared by package read and write.

Applies the per-entry structural rules (isValidToneParameterAutomation) and requires at most one
entry per (plugin, parameter) pair. Plugin ids are not resolved against tone documents; an id
without a live plugin is a legal unresolved entry by design.

\param entries Parsed or about-to-be-persisted automation entries.
\param tempo_map Tempo map the automation points must address.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateToneAutomationEntries(
    const std::vector<ToneParameterAutomation>& entries, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
