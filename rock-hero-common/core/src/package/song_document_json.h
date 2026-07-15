/*!
\file song_document_json.h
\brief Song-document JSON walks shared by the full package reader and the description peek.

Private to rock_hero_common_core. Both read paths must speak identical field vocabulary — the
full reader (rock_song_package_read.cpp) and the description peek (package_description.cpp) —
so the shared walks live here instead of diverging copies.
*/

#pragma once

#include <expected>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::common::core
{

/*!
\brief The single home of today's hard `formatVersion == 1` check.

docs/plans/roadmap/10-format-versioning-and-chart-identity.md replaces this ONE helper with its
tolerance/migration ladder; no other call site may test the version.

\param song_document Parsed song.json document root.
\return Empty success, or the typed unsupported-version failure.
*/
[[nodiscard]] std::expected<void, SongPackageError> requireSupportedSongDocumentVersion(
    const juce::var& song_document);

/*!
\brief Reads song metadata while treating missing descriptive fields as blank draft values.
\param song_document Parsed song.json document root.
\return Parsed metadata; blank fields when the metadata object is absent.
*/
[[nodiscard]] SongMetadata readSongDocumentMetadata(const juce::var& song_document);

} // namespace rock_hero::common::core
