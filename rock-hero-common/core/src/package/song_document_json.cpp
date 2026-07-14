#include "song_document_json.h"

#include <rock_hero/common/core/shared/json.h>

namespace rock_hero::common::core
{

// The ONE version gate plan 10's tolerance ladder later replaces; nothing else may test it.
std::expected<void, SongPackageError> requireSupportedSongDocumentVersion(
    const juce::var& song_document)
{
    const auto format_version = Json::readOptionalInt(song_document, "formatVersion", 0);
    if (!song_document.isObject() || format_version != 1)
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "Unsupported song.json formatVersion",
        }};
    }
    return {};
}

// Missing descriptive fields read as blank draft values rather than failures.
SongMetadata readSongDocumentMetadata(const juce::var& song_document)
{
    const juce::var& metadata = Json::value(song_document, "metadata");
    if (!metadata.isObject())
    {
        return {};
    }

    return SongMetadata{
        .title = Json::readOptionalString(metadata, "title"),
        .artist = Json::readOptionalString(metadata, "artist"),
        .album = Json::readOptionalString(metadata, "album"),
        .year = Json::readOptionalInt(metadata, "year", 0),
    };
}

// The persisted part vocabulary; unsupported tokens surface to each caller's own policy.
std::optional<Part> parseSongDocumentPart(const std::string& text)
{
    if (text == "Lead")
    {
        return Part::Lead;
    }

    if (text == "Rhythm")
    {
        return Part::Rhythm;
    }

    if (text == "Bass")
    {
        return Part::Bass;
    }

    return std::nullopt;
}

} // namespace rock_hero::common::core
