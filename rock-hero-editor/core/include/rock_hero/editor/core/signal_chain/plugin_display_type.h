/*!
\file plugin_display_type.h
\brief Canonical editor display types for scanned and loaded plugins.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Stable plugin type used by plugin-browser filters and signal-chain icons. */
enum class PluginDisplayType : std::uint8_t
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

/*! \brief Scanner metadata used to derive the canonical display type. */
struct PluginDisplayMetadata
{
    /*! \brief Raw scanner category metadata, such as Fx|Delay. */
    std::string_view category;
};

/*! \brief Canonical display classification derived for one plugin. */
struct PluginDisplayClassification
{
    /*! \brief Single display type used for compact signal-chain icon rendering. */
    PluginDisplayType primary_type{PluginDisplayType::Uncategorized};

    /*! \brief Rock Hero display types recognized directly from scanner category metadata. */
    std::vector<PluginDisplayType> scanned_types{};

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
\brief Derives canonical display types from scanner metadata.

The classifier recognizes only direct VST3 PlugType category tokens that have an equivalent
PluginDisplayType. Unknown, generic, and unsupported scanner tokens are ignored. A plugin is
auto-classified only when exactly one Rock Hero display type remains after that filtering.
Ambiguous or unmatched metadata returns PluginDisplayType::Uncategorized while preserving the
recognized scanner matches for UI suggestions.

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
