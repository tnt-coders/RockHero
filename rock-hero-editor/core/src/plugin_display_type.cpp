#include "plugin_display_type.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Exact overrides cover plugins whose scanner categories describe bundled effects instead of the
// product's most useful top-level role.
struct PluginDisplayTypeOverride
{
    std::string_view name;
    std::string_view manufacturer;
    PluginDisplayType primary_type;
};

constexpr PluginDisplayTypeOverride g_display_type_overrides[] = {
    {"Archetype Abasi", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Cory Wong X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Gojira X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Mateus Asato", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Misha Mansoor X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Nolly X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Petrucci X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Plini X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Rabea X", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Tim Henson", "Neural DSP", PluginDisplayType::Amp},
    {"Archetype Tom Morello", "Neural DSP", PluginDisplayType::Amp},
    {"Darkglass Ultra", "Neural DSP", PluginDisplayType::Amp},
    {"Emissary", "Ignite", PluginDisplayType::Amp},
    {"Emissary", "Ignite Amps", PluginDisplayType::Amp},
    {"Fortin Cali Suite", "Neural DSP", PluginDisplayType::Amp},
    {"Fortin Nameless Suite X", "Neural DSP", PluginDisplayType::Amp},
    {"Fortin NTS Suite", "Neural DSP", PluginDisplayType::Amp},
    {"Gateway", "Atkinson Advanced Modeling", PluginDisplayType::Amp},
    {"Gateway", "Atkinson Advanced Modeling, LLC", PluginDisplayType::Amp},
    {"Ignite - Emissary", "", PluginDisplayType::Amp},
    {"Ignite - Emissary", "Ignite", PluginDisplayType::Amp},
    {"Ignite - Emissary", "Ignite Amps", PluginDisplayType::Amp},
    {"Ignite - NadIR", "", PluginDisplayType::Cab},
    {"Ignite - NadIR", "Ignite", PluginDisplayType::Cab},
    {"Ignite - NadIR", "Ignite Amps", PluginDisplayType::Cab},
    {"Mesa Boogie Mark IIC+ Suite", "Neural DSP", PluginDisplayType::Amp},
    {"Morgan Amps Suite", "Neural DSP", PluginDisplayType::Amp},
    {"NadIR", "Ignite", PluginDisplayType::Cab},
    {"NadIR", "Ignite Amps", PluginDisplayType::Cab},
    {"Neural Amp Modeler", "Steven Atkinson", PluginDisplayType::Amp},
    {"NeuralAmpModeler", "Steven Atkinson", PluginDisplayType::Amp},
    {"OMEGA Ampworks Granophyre", "Neural DSP", PluginDisplayType::Amp},
    {"ParametricOD", "Steven Atkinson", PluginDisplayType::Distortion},
    {"Parallax X", "Neural DSP", PluginDisplayType::Amp},
    {"Soldano SLO-100 X", "Neural DSP", PluginDisplayType::Amp},
    {"Tone King Imperial MKII", "Neural DSP", PluginDisplayType::Amp},
};

// Normalizes scanner text for category lookup and exact override comparison.
[[nodiscard]] std::string normalizedToken(std::string_view text, bool strip_punctuation = false)
{
    std::string normalized;
    normalized.reserve(text.size());

    bool pending_space = false;
    for (const char character : text)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isspace(byte) != 0 || (strip_punctuation && std::ispunct(byte) != 0))
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

// Compares scanner metadata text with override keys after case, punctuation, and whitespace
// normalization.
[[nodiscard]] bool normalizedEquals(std::string_view lhs, std::string_view rhs)
{
    return normalizedToken(lhs, true) == normalizedToken(rhs, true);
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

// Filters VST3 container and routing markers that are not useful user-facing plugin types.
[[nodiscard]] bool isIgnoredCategoryToken(std::string_view token)
{
    return token == "fx" || token == "effect" || token == "mono" || token == "stereo" ||
           token == "surround" || token == "noofflineprocess" || token == "onlyara" ||
           token == "onlyofflineprocess" || token == "onlyrt";
}

// Maps a normalized category token to the canonical display type table.
[[nodiscard]] std::optional<PluginDisplayType> displayTypeForCategoryToken(std::string_view token)
{
    if (token == "amp" || token == "amplifier" || token == "amp simulator" ||
        token == "guitar amp" || token == "guitar amplifier")
    {
        return PluginDisplayType::Amp;
    }
    if (token == "cab" || token == "cabinet" || token == "impulse response" || token == "ir loader")
    {
        return PluginDisplayType::Cab;
    }
    if (token == "distortion" || token == "drive" || token == "fuzz" || token == "overdrive" ||
        token == "boost")
    {
        return PluginDisplayType::Distortion;
    }
    if (token == "delay" || token == "echo")
    {
        return PluginDisplayType::Delay;
    }
    if (token == "reverb" || token == "room" || token == "hall" || token == "plate")
    {
        return PluginDisplayType::Reverb;
    }
    if (token == "modulation" || token == "chorus" || token == "flanger" || token == "phaser" ||
        token == "tremolo" || token == "vibrato")
    {
        return PluginDisplayType::Modulation;
    }
    if (token == "dynamics" || token == "compressor" || token == "limiter")
    {
        return PluginDisplayType::Dynamics;
    }
    if (token == "eq" || token == "equalizer")
    {
        return PluginDisplayType::Eq;
    }
    if (token == "gate" || token == "noise" || token == "restoration")
    {
        return PluginDisplayType::Gate;
    }
    if (token == "pitch" || token == "pitch shift" || token == "octave" || token == "harmonizer")
    {
        return PluginDisplayType::Pitch;
    }
    if (token == "filter" || token == "wah")
    {
        return PluginDisplayType::Filter;
    }
    if (token == "instrument" || token == "synth" || token == "sampler" || token == "piano" ||
        token == "drum" || token == "generator")
    {
        return PluginDisplayType::Instrument;
    }

    return std::nullopt;
}

// Appends one display type once while preserving first-seen order.
void appendUniqueDisplayType(
    std::vector<PluginDisplayType>& display_types, PluginDisplayType display_type)
{
    if (std::ranges::find(display_types, display_type) == display_types.end())
    {
        display_types.push_back(display_type);
    }
}

// Finds an exact plugin override for known plugins with ambiguous or overly broad categories.
[[nodiscard]] std::optional<PluginDisplayType> displayTypeOverrideFor(
    const PluginDisplayMetadata& metadata)
{
    for (const PluginDisplayTypeOverride& override_entry : g_display_type_overrides)
    {
        const bool manufacturer_matches =
            override_entry.manufacturer.empty() ||
            normalizedEquals(metadata.manufacturer, override_entry.manufacturer);
        if (normalizedEquals(metadata.name, override_entry.name) && manufacturer_matches)
        {
            return override_entry.primary_type;
        }
    }

    return std::nullopt;
}

} // namespace

PluginDisplayClassification classifyPluginDisplay(const PluginDisplayMetadata& metadata)
{
    PluginDisplayClassification classification{
        .primary_type = PluginDisplayType::Uncategorized,
        .scanned_types = {},
        .filter_types = {},
    };

    const std::optional<PluginDisplayType> type_override = displayTypeOverrideFor(metadata);
    if (type_override.has_value())
    {
        classification.primary_type = *type_override;
        appendUniqueDisplayType(classification.filter_types, *type_override);
    }

    for (const std::string& token : categoryTokens(metadata.category))
    {
        if (isIgnoredCategoryToken(token))
        {
            continue;
        }

        if (const std::optional<PluginDisplayType> display_type =
                displayTypeForCategoryToken(token);
            display_type.has_value())
        {
            appendUniqueDisplayType(classification.scanned_types, *display_type);
            appendUniqueDisplayType(classification.filter_types, *display_type);
        }
    }

    if (classification.filter_types.empty())
    {
        classification.filter_types.push_back(PluginDisplayType::Uncategorized);
    }

    if (!type_override.has_value())
    {
        classification.primary_type = classification.filter_types.front();
    }

    return classification;
}

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
