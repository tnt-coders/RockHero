#include "song_information/song_file_name.h"

#include <rock_hero/common/core/shared/legal_file_name.h>
#include <rock_hero/common/core/shared/whitespace_trim.h>
#include <string_view>

namespace rock_hero::editor::core
{

namespace
{

// One half of the name: the field as stored, trimmed, or the placeholder when that leaves nothing.
[[nodiscard]] std::string_view nameHalf(
    const std::string_view field, const std::string_view placeholder) noexcept
{
    const std::string_view trimmed = common::core::trimmedWhitespace(field);
    return trimmed.empty() ? placeholder : trimmed;
}

} // namespace

// The legal-name rule runs over the whole stem, so the " - " between the halves is judged with
// them and the extension is appended after the cap.
std::string suggestedProjectFileName(const common::core::SongMetadata& metadata)
{
    std::string stem{nameHalf(metadata.artist, "Unknown Artist")};
    stem += " - ";
    stem += nameHalf(metadata.title, "Unknown Song");
    return common::core::legalFileName(stem) + ".rhp";
}

} // namespace rock_hero::editor::core
