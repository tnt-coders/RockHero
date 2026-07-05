#include "tracktion/tracktion_instrument_wave_device_mapping.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Creates a JUCE channel mask with the given physical channel bits enabled.
[[nodiscard]] juce::BigInteger channelMask(std::initializer_list<int> channels)
{
    juce::BigInteger mask;
    for (const int channel : channels)
    {
        mask.setBit(channel);
    }

    return mask;
}

// Builds instrument-route descriptions for the common valid test route.
[[nodiscard]] std::optional<InstrumentWaveDeviceDescriptions> describeRoute(
    std::initializer_list<int> input_channels, std::initializer_list<int> output_channels,
    const juce::StringArray& input_channel_names = {},
    const juce::StringArray& output_channel_names = {})
{
    return createTracktionInstrumentWaveDeviceDescriptions(
        "Example ASIO Device",
        channelMask(input_channels),
        channelMask(output_channels),
        input_channel_names,
        output_channel_names);
}

// Extracts a valid route while keeping optional precondition checks visible to clang-tidy.
[[nodiscard]] InstrumentWaveDeviceDescriptions requireRoute(
    std::optional<InstrumentWaveDeviceDescriptions> route)
{
    if (!route.has_value())
    {
        throw std::logic_error{"Expected an instrument route description"};
    }

    return std::move(route).value();
}

} // namespace

// Verifies the first physical input is exposed as the first compact callback channel.
TEST_CASE("Instrument route maps first input to compact mono", "[audio][instrument-route]")
{
    const InstrumentWaveDeviceDescriptions route = requireRoute(describeRoute({0}, {0, 1}));

    CHECK(route.route_mask.input_physical_channel == 0);
    REQUIRE(route.input.channels.size() == 1);
    CHECK(route.input.channels[0].compact_device_channel == 0);
    CHECK(route.input.channels[0].role == InstrumentChannelRole::Left);
}

// Verifies non-first physical inputs still map to Tracktion's compact mono channel.
TEST_CASE("Instrument route maps non-first input compactly", "[audio][instrument-route]")
{
    const InstrumentWaveDeviceDescriptions route = requireRoute(describeRoute({3}, {0, 1}));

    CHECK(route.route_mask.input_physical_channel == 3);
    REQUIRE(route.input.channels.size() == 1);
    CHECK(route.input.channels[0].compact_device_channel == 0);
    CHECK(route.input.channels[0].role == InstrumentChannelRole::Left);
}

// Verifies non-first physical output pairs become compact stereo output channels.
TEST_CASE("Instrument route maps non-first output compactly", "[audio][instrument-route]")
{
    const InstrumentWaveDeviceDescriptions route = requireRoute(describeRoute({0}, {6, 7}));

    CHECK(route.route_mask.output_left_physical_channel == 6);
    CHECK(route.route_mask.output_right_physical_channel == 7);
    REQUIRE(route.output.channels.size() == 2);
    CHECK(route.output.channels[0].compact_device_channel == 0);
    CHECK(route.output.channels[0].role == InstrumentChannelRole::Left);
    CHECK(route.output.channels[1].compact_device_channel == 1);
    CHECK(route.output.channels[1].role == InstrumentChannelRole::Right);
}

// Verifies generated Tracktion names carry hardware and physical channel identity.
TEST_CASE("Instrument route names include hardware and channels", "[audio][instrument-route]")
{
    const InstrumentWaveDeviceDescriptions route = requireRoute(describeRoute(
        {3},
        {6, 7},
        juce::StringArray{"Input 0", "Input 1", "Input 2", "Hi-Z"},
        juce::StringArray{"Out 0", "Out 1", "Out 2", "Out 3", "Out 4", "Out 5", "L", "R"}));

    CHECK(route.input.name == "Example ASIO Device Input 3 - Hi-Z");
    CHECK(route.output.name == "Example ASIO Device Output 6-7 - L / R");
}

// Verifies the route contract rejects a missing input channel.
TEST_CASE("Instrument route rejects missing input", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({}, {0, 1}).has_value());
}

// Verifies the route contract rejects multiple input channels.
TEST_CASE("Instrument route rejects multiple inputs", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({0, 1}, {0, 1}).has_value());
}

// Verifies the route contract rejects a missing output pair.
TEST_CASE("Instrument route rejects missing output", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({0}, {}).has_value());
}

// Verifies the route contract rejects non-adjacent output channel selections.
TEST_CASE("Instrument route rejects non-adjacent output", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({0}, {0, 2}).has_value());
}

// Verifies the route contract rejects adjacent channels that are not a UI stereo pair.
TEST_CASE("Instrument route rejects offset adjacent output", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({0}, {1, 2}).has_value());
}

// Verifies the route contract rejects more than one stereo output pair.
TEST_CASE("Instrument route rejects multiple output pairs", "[audio][instrument-route]")
{
    CHECK_FALSE(describeRoute({0}, {0, 1, 2, 3}).has_value());
}

} // namespace rock_hero::common::audio
