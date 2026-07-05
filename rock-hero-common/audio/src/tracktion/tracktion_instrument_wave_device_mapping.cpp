#include "tracktion/tracktion_instrument_wave_device_mapping.h"

#include <array>

namespace rock_hero::common::audio
{

namespace
{

// Finds the only enabled bit in a mask, rejecting empty or multi-channel selections.
[[nodiscard]] std::optional<int> findOnlyEnabledChannel(const juce::BigInteger& channels)
{
    if (channels.countNumberOfSetBits() != 1)
    {
        return std::nullopt;
    }

    return channels.findNextSetBit(0);
}

// Finds one adjacent stereo pair and rejects all other output-channel masks.
[[nodiscard]] std::optional<std::array<int, 2>> findOnlyAdjacentOutputPair(
    const juce::BigInteger& channels)
{
    if (channels.countNumberOfSetBits() != 2)
    {
        return std::nullopt;
    }

    const int left_channel = channels.findNextSetBit(0);
    const int right_channel = channels.findNextSetBit(left_channel + 1);
    if (left_channel % 2 != 0 || right_channel != left_channel + 1)
    {
        return std::nullopt;
    }

    return std::array{left_channel, right_channel};
}

// Uses the hardware name as part of Tracktion device identity, with a stable fallback for tests.
[[nodiscard]] juce::String normalizedDeviceName(const juce::String& hardware_device_name)
{
    juce::String trimmed_name = hardware_device_name.trim();
    if (trimmed_name.isNotEmpty())
    {
        return trimmed_name;
    }

    return "Audio Device";
}

// Returns a physical channel name only when the device reported one for that index.
[[nodiscard]] juce::String channelNameAt(
    const juce::StringArray& channel_names, int physical_channel)
{
    if (!juce::isPositiveAndBelow(physical_channel, channel_names.size()))
    {
        return {};
    }

    return channel_names[physical_channel].trim();
}

// Appends a user-facing physical channel name to the stable generated wave-device name.
[[nodiscard]] juce::String appendChannelName(
    juce::String base_name, const juce::String& channel_name)
{
    if (channel_name.isNotEmpty())
    {
        base_name << " - " << channel_name;
    }

    return base_name;
}

// Appends both output channel names when the current device reports them.
[[nodiscard]] juce::String appendOutputChannelNames(
    juce::String base_name, const juce::String& left_name, const juce::String& right_name)
{
    if (left_name.isEmpty() && right_name.isEmpty())
    {
        return base_name;
    }

    base_name << " - ";

    if (left_name.isNotEmpty())
    {
        base_name << left_name;
    }

    if (left_name.isNotEmpty() && right_name.isNotEmpty())
    {
        base_name << " / ";
    }

    if (right_name.isNotEmpty())
    {
        base_name << right_name;
    }

    return base_name;
}

// Creates the stable generated name for the selected mono input.
[[nodiscard]] juce::String makeInputName(
    const juce::String& hardware_device_name, int physical_channel,
    const juce::StringArray& input_channel_names)
{
    juce::String name = normalizedDeviceName(hardware_device_name);
    name << " Input " << physical_channel;

    return appendChannelName(name, channelNameAt(input_channel_names, physical_channel));
}

// Creates the stable generated name for the selected stereo output pair.
[[nodiscard]] juce::String makeOutputName(
    const juce::String& hardware_device_name, int left_channel, int right_channel,
    const juce::StringArray& output_channel_names)
{
    juce::String name = normalizedDeviceName(hardware_device_name);
    name << " Output " << left_channel << "-" << right_channel;

    return appendOutputChannelNames(
        name,
        channelNameAt(output_channel_names, left_channel),
        channelNameAt(output_channel_names, right_channel));
}

} // namespace

// Converts the app's physical one-input/one-output-pair contract to compact Tracktion devices.
std::optional<InstrumentWaveDeviceDescriptions> createTracktionInstrumentWaveDeviceDescriptions(
    const juce::String& hardware_device_name, const juce::BigInteger& active_input_channels,
    const juce::BigInteger& active_output_channels, const juce::StringArray& input_channel_names,
    const juce::StringArray& output_channel_names)
{
    const std::optional<int> input_channel = findOnlyEnabledChannel(active_input_channels);
    const std::optional<std::array<int, 2>> output_pair =
        findOnlyAdjacentOutputPair(active_output_channels);
    if (!input_channel.has_value() || !output_pair.has_value())
    {
        return std::nullopt;
    }

    return InstrumentWaveDeviceDescriptions{
        .route_mask =
            InstrumentRouteMask{
                .input_physical_channel = *input_channel,
                .output_left_physical_channel = (*output_pair)[0],
                .output_right_physical_channel = (*output_pair)[1],
            },
        .input =
            InstrumentWaveDescription{
                .name = makeInputName(hardware_device_name, *input_channel, input_channel_names),
                .channels =
                    {
                        InstrumentChannelDescription{
                            .compact_device_channel = 0,
                            .role = InstrumentChannelRole::Left,
                        },
                    },
            },
        .output = InstrumentWaveDescription{
            .name = makeOutputName(
                hardware_device_name, (*output_pair)[0], (*output_pair)[1], output_channel_names),
            .channels = {
                InstrumentChannelDescription{
                    .compact_device_channel = 0,
                    .role = InstrumentChannelRole::Left,
                },
                InstrumentChannelDescription{
                    .compact_device_channel = 1,
                    .role = InstrumentChannelRole::Right,
                },
            },
        },
    };
}

} // namespace rock_hero::common::audio
