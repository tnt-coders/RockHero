#include "signal_chain/plugin_display_type.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Normalizes scanner tokens for direct VST3 PlugType matching.
[[nodiscard]] std::string normalizedToken(std::string_view text)
{
    std::string normalized;
    normalized.reserve(text.size());

    bool pending_space = false;
    for (const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0)
        {
            pending_space = !normalized.empty();
            continue;
        }

        if (pending_space)
        {
            normalized.push_back(' ');
            pending_space = false;
        }

        normalized.push_back(static_cast<char>(std::tolower(byte)));
    }

    return normalized;
}

// Appends a category token after trimming whitespace-only fragments.
void appendCategoryToken(std::vector<std::string>& tokens, std::string_view token)
{
    const std::string normalized = normalizedToken(token);
    if (!normalized.empty())
    {
        tokens.push_back(normalized);
    }
}

// Splits VST3-style category metadata into normalized tokens.
[[nodiscard]] std::vector<std::string> categoryTokens(std::string_view category)
{
    std::vector<std::string> tokens;
    std::size_t token_start = 0;
    for (std::size_t index = 0; index < category.size(); ++index)
    {
        const char character = category[index];
        if (character == '|' || character == ',' || character == ';')
        {
            appendCategoryToken(tokens, category.substr(token_start, index - token_start));
            token_start = index + 1;
        }
    }

    appendCategoryToken(tokens, category.substr(token_start));
    return tokens;
}

// Maps a normalized direct VST3 PlugType token to the matching Rock Hero display type.
[[nodiscard]] std::optional<PluginDisplayType> displayTypeForCategoryToken(std::string_view token)
{
    if (token == "delay")
    {
        return PluginDisplayType::Delay;
    }
    if (token == "distortion")
    {
        return PluginDisplayType::Distortion;
    }
    if (token == "dynamics")
    {
        return PluginDisplayType::Dynamics;
    }
    if (token == "eq")
    {
        return PluginDisplayType::Eq;
    }
    if (token == "filter")
    {
        return PluginDisplayType::Filter;
    }
    if (token == "modulation")
    {
        return PluginDisplayType::Modulation;
    }
    if (token == "pitch shift")
    {
        return PluginDisplayType::Pitch;
    }
    if (token == "reverb")
    {
        return PluginDisplayType::Reverb;
    }
    if (token == "instrument" || token == "drum" || token == "piano" || token == "sampler" ||
        token == "synth")
    {
        return PluginDisplayType::Instrument;
    }

    return std::nullopt;
}

// Appends one display type once while preserving first-seen scanner order.
void appendUniqueDisplayType(
    std::vector<PluginDisplayType>& display_types, PluginDisplayType display_type)
{
    if (std::ranges::find(display_types, display_type) == display_types.end())
    {
        display_types.push_back(display_type);
    }
}

} // namespace

// Classifies scanner metadata conservatively so ambiguous plugins require user choice.
PluginDisplayClassification classifyPluginDisplay(const PluginDisplayMetadata& metadata)
{
    std::vector<PluginDisplayType> scanned_types;
    for (const std::string& token : categoryTokens(metadata.category))
    {
        if (const std::optional<PluginDisplayType> display_type =
                displayTypeForCategoryToken(token);
            display_type.has_value())
        {
            appendUniqueDisplayType(scanned_types, *display_type);
        }
    }

    const PluginDisplayType primary_type =
        scanned_types.size() == 1 ? scanned_types.front() : PluginDisplayType::Uncategorized;
    return PluginDisplayClassification{
        .primary_type = primary_type,
        .scanned_types = std::move(scanned_types),
    };
}

// Returns stable persisted tokens for editor-owned manual display type overrides.
std::string_view pluginDisplayTypeToken(PluginDisplayType display_type) noexcept
{
    switch (display_type)
    {
        case PluginDisplayType::Uncategorized:
        {
            return "uncategorized";
        }
        case PluginDisplayType::Amp:
        {
            return "amp";
        }
        case PluginDisplayType::Cab:
        {
            return "cab";
        }
        case PluginDisplayType::Distortion:
        {
            return "distortion";
        }
        case PluginDisplayType::Delay:
        {
            return "delay";
        }
        case PluginDisplayType::Reverb:
        {
            return "reverb";
        }
        case PluginDisplayType::Modulation:
        {
            return "modulation";
        }
        case PluginDisplayType::Dynamics:
        {
            return "dynamics";
        }
        case PluginDisplayType::Eq:
        {
            return "eq";
        }
        case PluginDisplayType::Gate:
        {
            return "gate";
        }
        case PluginDisplayType::Pitch:
        {
            return "pitch";
        }
        case PluginDisplayType::Filter:
        {
            return "filter";
        }
        case PluginDisplayType::Instrument:
        {
            return "instrument";
        }
    }

    return "uncategorized";
}

// Parses editor-owned manual display type override tokens.
std::optional<PluginDisplayType> pluginDisplayTypeFromToken(std::string_view token)
{
    const std::string normalized = normalizedToken(token);
    if (normalized == "uncategorized")
    {
        return PluginDisplayType::Uncategorized;
    }
    if (normalized == "amp")
    {
        return PluginDisplayType::Amp;
    }
    if (normalized == "cab")
    {
        return PluginDisplayType::Cab;
    }
    if (normalized == "distortion")
    {
        return PluginDisplayType::Distortion;
    }
    if (normalized == "delay")
    {
        return PluginDisplayType::Delay;
    }
    if (normalized == "reverb")
    {
        return PluginDisplayType::Reverb;
    }
    if (normalized == "modulation")
    {
        return PluginDisplayType::Modulation;
    }
    if (normalized == "dynamics")
    {
        return PluginDisplayType::Dynamics;
    }
    if (normalized == "eq")
    {
        return PluginDisplayType::Eq;
    }
    if (normalized == "gate")
    {
        return PluginDisplayType::Gate;
    }
    if (normalized == "pitch")
    {
        return PluginDisplayType::Pitch;
    }
    if (normalized == "filter")
    {
        return PluginDisplayType::Filter;
    }
    if (normalized == "instrument")
    {
        return PluginDisplayType::Instrument;
    }

    return std::nullopt;
}

// Centralizes display labels used by browser filters and signal-chain menus.
std::string pluginDisplayTypeLabel(PluginDisplayType display_type)
{
    switch (display_type)
    {
        case PluginDisplayType::Uncategorized:
        {
            return "Uncategorized";
        }
        case PluginDisplayType::Amp:
        {
            return "Amp";
        }
        case PluginDisplayType::Cab:
        {
            return "Cab";
        }
        case PluginDisplayType::Distortion:
        {
            return "Distortion";
        }
        case PluginDisplayType::Delay:
        {
            return "Delay";
        }
        case PluginDisplayType::Reverb:
        {
            return "Reverb";
        }
        case PluginDisplayType::Modulation:
        {
            return "Modulation";
        }
        case PluginDisplayType::Dynamics:
        {
            return "Dynamics";
        }
        case PluginDisplayType::Eq:
        {
            return "EQ";
        }
        case PluginDisplayType::Gate:
        {
            return "Gate";
        }
        case PluginDisplayType::Pitch:
        {
            return "Pitch";
        }
        case PluginDisplayType::Filter:
        {
            return "Filter";
        }
        case PluginDisplayType::Instrument:
        {
            return "Instrument";
        }
    }

    return "Uncategorized";
}

} // namespace rock_hero::editor::core
