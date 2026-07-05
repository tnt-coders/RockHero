/*!
\file tracktion_instrument_wave_device_mapping.h
\brief Testable mapping from JUCE active channel masks to Tracktion wave-device descriptions.
*/

#pragma once

#include <cstdint>
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace rock_hero::common::audio
{

/*! \brief Semantic role of one compact channel in a generated wave-device description. */
enum class InstrumentChannelRole : std::uint8_t
{
    /*! \brief Left or mono-left channel. */
    Left,

    /*! \brief Right channel. */
    Right,
};

/*!
\brief One compact callback channel exposed to Tracktion for the instrument route.
*/
struct InstrumentChannelDescription
{
    /*! \brief Index in JUCE's compact selected-channel callback buffer. */
    int compact_device_channel{};

    /*! \brief Channel role used to build Tracktion's AudioChannelSet. */
    InstrumentChannelRole role{InstrumentChannelRole::Left};
};

/*!
\brief Physical device channels selected by the project-level instrument route.
*/
struct InstrumentRouteMask
{
    /*! \brief Selected physical mono input channel bit. */
    int input_physical_channel{};

    /*! \brief Selected physical left output channel bit. */
    int output_left_physical_channel{};

    /*! \brief Selected physical right output channel bit. */
    int output_right_physical_channel{};
};

/*!
\brief Project-owned description of one generated Tracktion wave input or output.
*/
struct InstrumentWaveDescription
{
    /*! \brief Stable hardware-qualified name used for Tracktion wave-device identity. */
    juce::String name;

    /*! \brief Compact callback channels that make up this generated wave device. */
    std::vector<InstrumentChannelDescription> channels;
};

/*!
\brief Generated mono input and stereo output descriptions for the active instrument route.
*/
struct InstrumentWaveDeviceDescriptions
{
    /*! \brief Physical channel mask selected by the user-facing audio settings. */
    InstrumentRouteMask route_mask;

    /*! \brief Compact mono input description for the instrument track. */
    InstrumentWaveDescription input;

    /*! \brief Compact stereo output description for the full mix output. */
    InstrumentWaveDescription output;
};

/*!
\brief Builds compact instrument wave-device descriptions from current JUCE masks.

\param hardware_device_name Name reported by the current JUCE audio device.
\param active_input_channels Active physical input-channel mask.
\param active_output_channels Active physical output-channel mask.
\param input_channel_names Physical input channel names reported by the current device.
\param output_channel_names Physical output channel names reported by the current device.
\return Generated descriptions, or empty when the masks do not match the app contract.
*/
[[nodiscard]] std::optional<InstrumentWaveDeviceDescriptions>
createTracktionInstrumentWaveDeviceDescriptions(
    const juce::String& hardware_device_name, const juce::BigInteger& active_input_channels,
    const juce::BigInteger& active_output_channels, const juce::StringArray& input_channel_names,
    const juce::StringArray& output_channel_names);

} // namespace rock_hero::common::audio
