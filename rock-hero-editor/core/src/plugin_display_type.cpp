#include "plugin_display_type.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/json.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

constexpr std::string_view g_config_version_property{"version"};
constexpr std::string_view g_config_overrides_property{"overrides"};
constexpr std::string_view g_config_name_property{"name"};
constexpr std::string_view g_config_type_property{"type"};

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

// Formats a zero-based override index for user-facing config diagnostics.
[[nodiscard]] std::string overrideIndexText(std::size_t index)
{
    return "override " + std::to_string(index + 1);
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
    const PluginDisplayMetadata& metadata, const PluginDisplayTypeOverrides& overrides)
{
    for (const PluginDisplayTypeOverride& override_entry : overrides)
    {
        if (normalizedEquals(metadata.name, override_entry.name))
        {
            return override_entry.display_type;
        }
    }

    return std::nullopt;
}

// Reads one override entry and rejects malformed config before it can affect classification.
[[nodiscard]] std::expected<PluginDisplayTypeOverride, PluginDisplayTypeConfigError>
readPluginDisplayTypeOverrideJson(const juce::var& value, std::size_t index)
{
    if (!value.isObject())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type " + overrideIndexText(index) + " is not an object.",
        }};
    }

    const std::optional<std::string> name =
        common::core::Json::tryReadString(value, g_config_name_property);
    if (!name.has_value() || name->empty())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type " + overrideIndexText(index) + " is missing a name.",
        }};
    }

    const std::optional<std::string> type_token =
        common::core::Json::tryReadString(value, g_config_type_property);
    if (!type_token.has_value() || type_token->empty())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type " + overrideIndexText(index) + " is missing a type.",
        }};
    }

    const std::optional<PluginDisplayType> display_type = pluginDisplayTypeFromToken(*type_token);
    if (!display_type.has_value() || *display_type == PluginDisplayType::Uncategorized)
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::UnknownType,
            "Plugin display type " + overrideIndexText(index) +
                " uses unknown type token: " + *type_token,
        }};
    }

    return PluginDisplayTypeOverride{
        .name = *name,
        .display_type = *display_type,
    };
}

} // namespace

// Supplies a generic config diagnostic when a caller only needs the stable code.
PluginDisplayTypeConfigError::PluginDisplayTypeConfigError(
    PluginDisplayTypeConfigErrorCode error_code)
    : PluginDisplayTypeConfigError(error_code, "Could not read plugin display type config")
{}

// Stores parser or file detail without making message text part of the branchable contract.
PluginDisplayTypeConfigError::PluginDisplayTypeConfigError(
    PluginDisplayTypeConfigErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

// Parses the shipped JSON config into an ordered override table.
std::expected<PluginDisplayTypeOverrides, PluginDisplayTypeConfigError>
readPluginDisplayTypeOverrides(std::string_view json_text)
{
    std::expected<juce::var, common::core::Json::Error> parsed =
        common::core::Json::parseUtf8Document(json_text);
    if (!parsed.has_value())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidJson,
            "Could not parse plugin display type config: " + parsed.error().message,
        }};
    }

    if (!parsed->isObject())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type config root must be an object.",
        }};
    }

    if (common::core::Json::readOptionalInt(*parsed, g_config_version_property, 0) != 1)
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type config must declare version 1.",
        }};
    }

    const juce::var& overrides_value =
        common::core::Json::value(*parsed, g_config_overrides_property);
    const juce::Array<juce::var>* const overrides_array = overrides_value.getArray();
    if (overrides_array == nullptr)
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::InvalidSchema,
            "Plugin display type config must contain an overrides array.",
        }};
    }

    PluginDisplayTypeOverrides overrides;
    overrides.reserve(static_cast<std::size_t>(overrides_array->size()));
    std::vector<std::string> override_names;
    override_names.reserve(static_cast<std::size_t>(overrides_array->size()));
    for (int index = 0; index < overrides_array->size(); ++index)
    {
        auto override_entry = readPluginDisplayTypeOverrideJson(
            (*overrides_array)[index], static_cast<std::size_t>(index));
        if (!override_entry.has_value())
        {
            return std::unexpected{std::move(override_entry.error())};
        }

        const std::string override_name = normalizedToken(override_entry->name, true);
        if (std::ranges::find(override_names, override_name) != override_names.end())
        {
            return std::unexpected{PluginDisplayTypeConfigError{
                PluginDisplayTypeConfigErrorCode::InvalidSchema,
                "Plugin display type " + overrideIndexText(static_cast<std::size_t>(index)) +
                    " duplicates an earlier name: " + override_entry->name,
            }};
        }

        override_names.push_back(override_name);
        overrides.push_back(std::move(*override_entry));
    }

    return overrides;
}

// Reads the shipped config file as UTF-8-ish text before applying schema validation.
std::expected<PluginDisplayTypeOverrides, PluginDisplayTypeConfigError>
readPluginDisplayTypeOverridesFile(const std::filesystem::path& file)
{
    std::ifstream stream{file, std::ios::binary};
    if (!stream.is_open())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::CouldNotReadFile,
            "Could not open plugin display type config: " + file.string(),
        }};
    }

    std::string contents{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
    if (!stream.good() && !stream.eof())
    {
        return std::unexpected{PluginDisplayTypeConfigError{
            PluginDisplayTypeConfigErrorCode::CouldNotReadFile,
            "Could not read plugin display type config: " + file.string(),
        }};
    }

    return readPluginDisplayTypeOverrides(contents);
}

// Classifies plugin metadata using caller-supplied exact override rows before category tokens.
PluginDisplayClassification classifyPluginDisplay(
    const PluginDisplayMetadata& metadata, const PluginDisplayTypeOverrides& overrides)
{
    PluginDisplayClassification classification{
        .primary_type = PluginDisplayType::Uncategorized,
        .scanned_types = {},
        .filter_types = {},
    };

    const std::optional<PluginDisplayType> type_override =
        displayTypeOverrideFor(metadata, overrides);
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

// Keeps category-only classification available for tests and missing-config fallback behavior.
PluginDisplayClassification classifyPluginDisplay(const PluginDisplayMetadata& metadata)
{
    return classifyPluginDisplay(metadata, PluginDisplayTypeOverrides{});
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
