#include "shared/legal_file_name.h"

#include <cstddef>

namespace rock_hero::common::core
{

namespace
{

// Comfortably inside the 255-byte component limit of every supported file system.
constexpr std::size_t g_max_stem_bytes{120};

// The characters at least one of Windows, macOS and Linux refuses in a file name.
constexpr std::string_view g_illegal_characters{"<>:\"/\\|?*"};

// Below 0x20 and 0x7F: control bytes. A multi-byte UTF-8 sequence never contains one.
[[nodiscard]] constexpr bool isControlByte(const unsigned char byte) noexcept
{
    return byte < 0x20U || byte == 0x7FU;
}

// A UTF-8 continuation byte, 10xxxxxx: the middle or end of a multi-byte character.
[[nodiscard]] constexpr bool isContinuationByte(const unsigned char byte) noexcept
{
    return (byte & 0xC0U) == 0x80U;
}

} // namespace

// One pass over the bytes, then the cap, so the cap measures the text actually written.
std::string legalFileName(const std::string_view stem)
{
    std::string legal;
    legal.reserve(stem.size());
    for (const char character : stem)
    {
        if (isControlByte(static_cast<unsigned char>(character)))
        {
            continue;
        }
        if (g_illegal_characters.find(character) != std::string_view::npos)
        {
            legal.push_back('_');
            continue;
        }
        legal.push_back(character);
    }

    if (legal.starts_with('.'))
    {
        legal.front() = '_';
    }

    if (legal.size() > g_max_stem_bytes)
    {
        // Back up past the continuation bytes of a character the cap would split, so the cut
        // falls before that character's lead byte.
        std::size_t cut = g_max_stem_bytes;
        while (cut > 0 && isContinuationByte(static_cast<unsigned char>(legal[cut])))
        {
            --cut;
        }
        legal.resize(cut);
    }

    return legal;
}

} // namespace rock_hero::common::core
