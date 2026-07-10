#include "package/package_id.h"

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
constexpr std::string_view g_chart_document_prefix{"charts/"};
constexpr std::string_view g_chart_document_suffix{".chart.json"};

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

std::string toneIdFromToneDocumentRef(std::string_view tone_document_ref)
{
    if (!isCanonicalToneDocumentRef(tone_document_ref))
    {
        return {};
    }

    return std::string{tone_document_ref.substr(g_tone_document_prefix.size(), g_uuid_length)};
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

    // remove_prefix/remove_suffix rather than substr: substr's out-of-range check makes it
    // potentially throwing, which clang-tidy 22 flags inside this noexcept function even though
    // the size check above rules the throw out.
    std::string_view uuid = tone_document_ref;
    uuid.remove_prefix(g_tone_document_prefix.size());
    uuid.remove_suffix(g_tone_document_suffix.size());
    return isCanonicalPackageId(uuid);
}

bool isCanonicalChartDocumentRef(std::string_view chart_document_ref) noexcept
{
    if (!chart_document_ref.starts_with(g_chart_document_prefix) ||
        !chart_document_ref.ends_with(g_chart_document_suffix) ||
        chart_document_ref.size() !=
            g_chart_document_prefix.size() + g_uuid_length + g_chart_document_suffix.size())
    {
        return false;
    }

    // remove_prefix/remove_suffix rather than substr: substr's out-of-range check makes it
    // potentially throwing, which clang-tidy 22 flags inside this noexcept function even though
    // the size check above rules the throw out.
    std::string_view uuid = chart_document_ref;
    uuid.remove_prefix(g_chart_document_prefix.size());
    uuid.remove_suffix(g_chart_document_suffix.size());
    return isCanonicalPackageId(uuid);
}

} // namespace rock_hero::common::core
