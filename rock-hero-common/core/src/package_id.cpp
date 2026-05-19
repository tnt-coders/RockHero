#include "package_id.h"

#include <cstddef>
#include <juce_core/juce_core.h>
#include <string>
#include <string_view>

namespace rock_hero::common::core
{

namespace
{

constexpr std::size_t g_uuid_length = 36;
constexpr std::string_view g_tone_document_prefix{"tones/"};
constexpr std::string_view g_tone_document_suffix{"/tone.json"};

[[nodiscard]] bool isLowercaseHex(char character) noexcept
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

[[nodiscard]] bool isDashIndex(std::size_t index) noexcept
{
    return index == 8 || index == 13 || index == 18 || index == 23;
}

[[nodiscard]] bool isRfc4122VariantNibble(char character) noexcept
{
    return character == '8' || character == '9' || character == 'a' || character == 'b';
}

} // namespace

std::string generatePackageId()
{
    return juce::Uuid{}.toDashedString().toStdString();
}

std::string toneDocumentRefForToneId(std::string_view tone_id)
{
    return std::string{g_tone_document_prefix} + std::string{tone_id} +
           std::string{g_tone_document_suffix};
}

bool isCanonicalPackageId(std::string_view id) noexcept
{
    if (id.size() != g_uuid_length)
    {
        return false;
    }

    for (std::size_t index = 0; index < id.size(); ++index)
    {
        const char character = id[index];
        if (isDashIndex(index))
        {
            if (character != '-')
            {
                return false;
            }
            continue;
        }

        if (!isLowercaseHex(character))
        {
            return false;
        }
    }

    return id[14] == '4' && isRfc4122VariantNibble(id[19]);
}

bool isCanonicalToneDocumentRef(std::string_view tone_document_ref) noexcept
{
    if (!tone_document_ref.starts_with(g_tone_document_prefix) ||
        !tone_document_ref.ends_with(g_tone_document_suffix) ||
        tone_document_ref.size() !=
            g_tone_document_prefix.size() + g_uuid_length + g_tone_document_suffix.size())
    {
        return false;
    }

    return isCanonicalPackageId(
        tone_document_ref.substr(g_tone_document_prefix.size(), g_uuid_length));
}

} // namespace rock_hero::common::core
