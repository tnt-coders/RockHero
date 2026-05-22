#include "audio_device_type_policy.h"

#include <catch2/catch_test_macros.hpp>

namespace rock_hero::editor::ui
{

// The default picker keeps every backend visible while putting recognized low-latency systems
// first.
TEST_CASE("Audio device type picker orders Windows systems", "[ui][audio-device]")
{
    const juce::StringArray available_types{
        "DirectSound",
        "WaveOut",
        "Windows Audio",
        "Windows Audio (Exclusive Mode)",
        "Windows Audio (Low Latency Mode)",
        "ASIO",
    };

    const juce::StringArray ordered_types = audioDeviceTypePickerOrder(available_types);

    CHECK(
        ordered_types == juce::StringArray{
                             "ASIO",
                             "Windows Audio (Low Latency Mode)",
                             "Windows Audio",
                             "Windows Audio (Exclusive Mode)",
                             "DirectSound",
                             "WaveOut",
                         });
}

// Shared WASAPI is preferred over Exclusive Mode because exclusive routes need extra device-side
// configuration to open reliably; the picker defaults to the variant most users can connect to.
TEST_CASE("Audio device type picker prefers shared WASAPI over exclusive", "[ui][audio-device]")
{
    const juce::StringArray available_types{
        "Windows Audio",
        "Windows Audio (Exclusive Mode)",
        "DirectSound",
    };

    const juce::StringArray ordered_types = audioDeviceTypePickerOrder(available_types);

    CHECK(
        ordered_types ==
        juce::StringArray{"Windows Audio", "Windows Audio (Exclusive Mode)", "DirectSound"});
}

// Unrecognized backend families keep their input order because the picker has no defensible
// reason to reorder them relative to each other.
TEST_CASE("Audio device type picker keeps non Windows systems", "[ui][audio-device]")
{
    const juce::StringArray available_types{"ALSA", "JACK"};

    const juce::StringArray ordered_types = audioDeviceTypePickerOrder(available_types);

    CHECK(ordered_types == available_types);
}

// Legacy Windows systems remain visible and are ordered for the picker default.
TEST_CASE("Audio device type picker orders legacy Windows systems", "[ui][audio-device]")
{
    const juce::StringArray available_types{"WaveOut", "DirectSound"};

    const juce::StringArray ordered_types = audioDeviceTypePickerOrder(available_types);

    CHECK(ordered_types == juce::StringArray{"DirectSound", "WaveOut"});
}

// Unrecognized backends sort after every recognized family, including legacy Windows backends,
// so the picker default never lands on a backend whose characteristics we cannot defend.
TEST_CASE("Audio device type picker sorts unknown backends last", "[ui][audio-device]")
{
    const juce::StringArray available_types{
        "MysteryBackend",
        "WaveOut",
        "ASIO",
        "DirectSound",
    };

    const juce::StringArray ordered_types = audioDeviceTypePickerOrder(available_types);

    CHECK(ordered_types == juce::StringArray{"ASIO", "DirectSound", "WaveOut", "MysteryBackend"});
}

} // namespace rock_hero::editor::ui
