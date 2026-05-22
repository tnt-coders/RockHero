#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/audio/audio_device_settings.h>
#include <vector>

namespace rock_hero::common::audio
{

// The default order keeps every backend visible while putting recognized low-latency systems
// first.
TEST_CASE("Audio device settings order Windows systems", "[audio][audio-device-settings]")
{
    const juce::StringArray available_types{
        "DirectSound",
        "WaveOut",
        "Windows Audio",
        "Windows Audio (Exclusive Mode)",
        "Windows Audio (Low Latency Mode)",
        "ASIO",
    };

    const juce::StringArray ordered_types = preferredAudioDeviceTypeOrder(available_types);

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
// configuration to open reliably; the default lands on the variant most users can connect to.
TEST_CASE("Audio device settings prefer shared WASAPI", "[audio][audio-device-settings]")
{
    const juce::StringArray available_types{
        "Windows Audio",
        "Windows Audio (Exclusive Mode)",
        "DirectSound",
    };

    const juce::StringArray ordered_types = preferredAudioDeviceTypeOrder(available_types);

    CHECK(
        ordered_types ==
        juce::StringArray{"Windows Audio", "Windows Audio (Exclusive Mode)", "DirectSound"});
}

// Unrecognized backend families keep their input order because the settings policy has no
// defensible reason to reorder them relative to each other.
TEST_CASE("Audio device settings keep unknown family order", "[audio][audio-device-settings]")
{
    const juce::StringArray available_types{"ALSA", "JACK"};

    const juce::StringArray ordered_types = preferredAudioDeviceTypeOrder(available_types);

    CHECK(ordered_types == available_types);
}

// Legacy Windows systems remain visible and are ordered for the settings default.
TEST_CASE("Audio device settings order legacy Windows systems", "[audio][audio-device-settings]")
{
    const juce::StringArray available_types{"WaveOut", "DirectSound"};

    const juce::StringArray ordered_types = preferredAudioDeviceTypeOrder(available_types);

    CHECK(ordered_types == juce::StringArray{"DirectSound", "WaveOut"});
}

// Unrecognized backends sort after every recognized family, including legacy Windows backends, so
// the default never lands on a backend whose characteristics we cannot defend.
TEST_CASE("Audio device settings sort unknown backends last", "[audio][audio-device-settings]")
{
    const juce::StringArray available_types{
        "MysteryBackend",
        "WaveOut",
        "ASIO",
        "DirectSound",
    };

    const juce::StringArray ordered_types = preferredAudioDeviceTypeOrder(available_types);

    CHECK(ordered_types == juce::StringArray{"ASIO", "DirectSound", "WaveOut", "MysteryBackend"});
}

// A staged rate that is still available wins over any other source.
TEST_CASE("Audio device settings keep staged sample rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 96000.0, 48000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// When no rate is staged but the preview device reports one, settings adopt it.
TEST_CASE("Audio device settings use preview sample rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 0.0, 96000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// A closed preview device can borrow the active route's rate when it describes the same route.
TEST_CASE("Audio device settings borrow active sample rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 0.0, 0.0, 48000.0);

    CHECK(selected == 48000.0);
}

// A closed preview device with no active route hint falls back to the studio-standard 48 kHz.
TEST_CASE("Audio device settings fall back to 48 kHz", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 48000.0);
}

// 44.1 kHz is the secondary studio-standard fallback when 48 kHz is not offered.
TEST_CASE("Audio device settings fall back to 44.1 kHz", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{22050.0, 44100.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 44100.0);
}

// With neither studio-standard rate available, settings default to the first offered choice.
TEST_CASE("Audio device settings fall back to first rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{22050.0, 32000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 22050.0);
}

// An unsupported staged rate falls through to the preview-device rate before generic fallbacks.
TEST_CASE("Audio device settings fall through staged rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 88200.0, 96000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// An unavailable preview-device rate falls through to a matching active-route rate.
TEST_CASE("Audio device settings fall through preview rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 96000.0, 88200.0, 44100.0);

    CHECK(selected == 44100.0);
}

// If no route-specific rate is available, settings still pick an available fallback.
TEST_CASE("Audio device settings discard unavailable rate", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected =
        chooseAudioDeviceSampleRate(available_rates, 96000.0, 0.0, std::nullopt);

    CHECK(selected == 48000.0);
}

// A device that reports no available rates yields a non-selection.
TEST_CASE("Audio device settings return zero for no rates", "[audio][audio-device-settings]")
{
    const std::vector<double> available_rates{};
    const double selected = chooseAudioDeviceSampleRate(available_rates, 48000.0, 48000.0, 48000.0);

    CHECK(selected == 0.0);
}

} // namespace rock_hero::common::audio
