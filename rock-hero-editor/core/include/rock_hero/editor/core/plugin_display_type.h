/*!
\file plugin_display_type.h
\brief Canonical editor display types for scanned and loaded plugins.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Stable plugin type used by plugin-browser filters and signal-chain icons. */
enum class PluginDisplayType
{
    /*! \brief The scanner did not provide a recognized type. */
    Uncategorized,

    /*! \brief Amplifier, amp simulator, or similar tone-shaping plugin. */
    Amp,

    /*! \brief Cabinet, impulse response, or speaker loader plugin. */
    Cab,

    /*! \brief Distortion, drive, fuzz, overdrive, or boost plugin. */
    Distortion,

    /*! \brief Delay or echo plugin. */
    Delay,

    /*! \brief Reverb or room simulation plugin. */
    Reverb,

    /*! \brief Chorus, flanger, phaser, tremolo, vibrato, or similar modulation plugin. */
    Modulation,

    /*! \brief Compressor, limiter, or similar dynamics plugin. */
    Dynamics,

    /*! \brief Equalizer plugin. */
    Eq,

    /*! \brief Noise gate or noise reduction plugin. */
    Gate,

    /*! \brief Pitch shifting, harmonizer, octave, or tuning plugin. */
    Pitch,

    /*! \brief Filter or wah plugin. */
    Filter,

    /*! \brief Instrument, synth, sampler, piano, drum, or generator plugin. */
    Instrument,
};

/*! \brief Stable failure reasons when reading plugin display type override config. */
enum class PluginDisplayTypeConfigErrorCode
{
    /*! \brief The override file could not be opened or read. */
    CouldNotReadFile,

    /*! \brief The override file was not valid JSON. */
    InvalidJson,

    /*! \brief The override JSON did not match the expected schema. */
    InvalidSchema,

    /*! \brief An override entry used an unknown display type token. */
    UnknownType,
};

/*! \brief Recoverable plugin display type config failure with stable code and detail. */
struct [[nodiscard]] PluginDisplayTypeConfigError
{
    /*!
    \brief Creates an error with the default message for the supplied code.
    \param error_code Stable failure code.
    */
    explicit PluginDisplayTypeConfigError(PluginDisplayTypeConfigErrorCode error_code);

    /*!
    \brief Creates an error with an explicit diagnostic message.
    \param error_code Stable failure code.
    \param message_text Display or log diagnostic.
    */
    PluginDisplayTypeConfigError(
        PluginDisplayTypeConfigErrorCode error_code, std::string message_text);

    /*! \brief Stable failure code. */
    PluginDisplayTypeConfigErrorCode code{};

    /*! \brief Display or log diagnostic. */
    std::string message;
};

/*! \brief Scanner metadata used to derive the canonical display type. */
struct PluginDisplayMetadata
{
    /*! \brief Opaque backend plugin identifier, when available. */
    std::string_view id;

    /*! \brief User-facing plugin name. */
    std::string_view name;

    /*! \brief User-facing manufacturer name. */
    std::string_view manufacturer;

    /*! \brief Backend format name, such as VST3. */
    std::string_view format_name;

    /*! \brief Raw scanner category metadata, such as Fx|Delay. */
    std::string_view category;
};

/*! \brief Exact plugin display type override read from default editor config. */
struct PluginDisplayTypeOverride
{
    /*! \brief User-facing plugin name to match after normalization. */
    std::string name;

    /*! \brief Display type used when the override matches. */
    PluginDisplayType display_type{PluginDisplayType::Uncategorized};

    /*!
    \brief Compares two display type overrides by their stored values.
    \param lhs Left-hand display type override.
    \param rhs Right-hand display type override.
    \return True when both overrides store equal values.
    */
    friend bool operator==(
        const PluginDisplayTypeOverride& lhs, const PluginDisplayTypeOverride& rhs) = default;
};

/*! \brief Plugin display type override table keyed by normalized plugin name. */
using PluginDisplayTypeOverrides = std::vector<PluginDisplayTypeOverride>;

/*! \brief Canonical display classification derived for one plugin. */
struct PluginDisplayClassification
{
    /*! \brief Single display type used for compact signal-chain icon rendering. */
    PluginDisplayType primary_type{PluginDisplayType::Uncategorized};

    /*! \brief Display types recognized directly from scanner category metadata. */
    std::vector<PluginDisplayType> scanned_types{};

    /*! \brief Display types available to the browser type filter. */
    std::vector<PluginDisplayType> filter_types{PluginDisplayType::Uncategorized};

    /*!
    \brief Compares two display classifications by their stored values.
    \param lhs Left-hand plugin display classification.
    \param rhs Right-hand plugin display classification.
    \return True when both classifications store equal values.
    */
    friend bool operator==(
        const PluginDisplayClassification& lhs, const PluginDisplayClassification& rhs) = default;
};

/*!
\brief Reads plugin display type overrides from JSON text.

The expected schema is an object with version 1 and an overrides array. Each override has a
required name and required stable type token. Names must be unique after normalization.

\param json_text UTF-8 JSON document text.
\return Parsed override table, or a config error.
*/
[[nodiscard]] std::expected<PluginDisplayTypeOverrides, PluginDisplayTypeConfigError>
readPluginDisplayTypeOverrides(std::string_view json_text);

/*!
\brief Reads plugin display type overrides from a JSON file.
\param file Override JSON file path.
\return Parsed override table, or a config error.
*/
[[nodiscard]] std::expected<PluginDisplayTypeOverrides, PluginDisplayTypeConfigError>
readPluginDisplayTypeOverridesFile(const std::filesystem::path& file);

/*!
\brief Derives canonical display types from scanner metadata and supplied exact overrides.

The classifier is deterministic: exact plugin overrides choose the primary type for known plugins,
recognized scanner category tokens provide filterable types, and unknown or generic-only metadata
returns PluginDisplayType::Uncategorized.

\param metadata Scanner metadata for one plugin.
\param overrides Exact plugin override table, matched by normalized plugin name.
\return Canonical display classification used by editor presentation.
*/
[[nodiscard]] PluginDisplayClassification classifyPluginDisplay(
    const PluginDisplayMetadata& metadata, const PluginDisplayTypeOverrides& overrides);

/*!
\brief Derives canonical display types from scanner metadata without config overrides.

This overload is useful for tests and fallback startup when the default override file cannot be
read. Unknown or generic-only metadata returns PluginDisplayType::Uncategorized.

\param metadata Scanner metadata for one plugin.
\return Canonical display classification used by editor presentation.
*/
[[nodiscard]] PluginDisplayClassification classifyPluginDisplay(
    const PluginDisplayMetadata& metadata);

/*!
\brief Returns the stable persisted token for a display type.
\param display_type Display type to serialize.
\return Stable lowercase token stored in editor-owned tone metadata.
*/
[[nodiscard]] std::string_view pluginDisplayTypeToken(PluginDisplayType display_type) noexcept;

/*!
\brief Parses a stable persisted display type token.
\param token Persisted token read from editor-owned tone metadata.
\return Matching display type, or empty when the token is unknown.
*/
[[nodiscard]] std::optional<PluginDisplayType> pluginDisplayTypeFromToken(std::string_view token);

/*!
\brief Returns the user-facing label for a canonical plugin display type.
\param display_type Display type to label.
\return Human-readable label for browser filters and plugin rows.
*/
[[nodiscard]] std::string pluginDisplayTypeLabel(PluginDisplayType display_type);

} // namespace rock_hero::editor::core
