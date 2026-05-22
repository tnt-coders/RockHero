#include "audio_device_sample_rate_choice.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <vector>

namespace rock_hero::editor::ui
{

// A staged rate that is still available wins over any other source.
TEST_CASE("Sample rate choice keeps the staged rate when available", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 96000.0, 48000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// When no rate is staged but the preview device reports one, the dialog adopts it.
TEST_CASE("Sample rate choice uses the preview device rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 0.0, 96000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// A closed staged device can borrow the active route's rate when its names match the open route.
TEST_CASE("Sample rate choice borrows the active route rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 0.0, 0.0, 48000.0);

    CHECK(selected == 48000.0);
}

// A closed staged device with no active route hint falls back to the studio-standard 48 kHz.
TEST_CASE(
    "Sample rate choice falls back to 48 kHz when no source rate is available",
    "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 48000.0);
}

// 44.1 kHz is the secondary studio-standard fallback when 48 kHz is not offered by the device.
TEST_CASE(
    "Sample rate choice falls back to 44.1 kHz when 48 kHz is unavailable",
    "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{22050.0, 44100.0};
    const double selected = chooseDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 44100.0);
}

// With neither studio-standard rate available the dialog defaults to the first offered choice.
TEST_CASE("Sample rate choice falls back to the first available rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{22050.0, 32000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 0.0, 0.0, std::nullopt);

    CHECK(selected == 22050.0);
}

// A staged rate the device no longer supports cannot be honored; the dialog picks an available
// preview-device rate before falling through to generic fallbacks.
TEST_CASE(
    "Sample rate choice falls through from unavailable staged rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0, 96000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 88200.0, 96000.0, 44100.0);

    CHECK(selected == 96000.0);
}

// A preview-device rate that is unavailable also falls through to the matching active-route rate.
TEST_CASE(
    "Sample rate choice falls through from unavailable preview rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 96000.0, 88200.0, 44100.0);

    CHECK(selected == 44100.0);
}

// A staged rate the device no longer supports cannot be honored; the dialog picks an available
// fallback so the combo never shows an absent selection.
TEST_CASE("Sample rate choice discards an unavailable staged rate", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{44100.0, 48000.0};
    const double selected = chooseDeviceSampleRate(available_rates, 96000.0, 0.0, std::nullopt);

    CHECK(selected == 48000.0);
}

// A device that reports no available rates yields a non-selection so the dialog stays honest
// about the empty state rather than persisting a zero rate.
TEST_CASE("Sample rate choice returns zero when no rates are offered", "[ui][sample-rate-choice]")
{
    const std::vector<double> available_rates{};
    const double selected = chooseDeviceSampleRate(available_rates, 48000.0, 48000.0, 48000.0);

    CHECK(selected == 0.0);
}

} // namespace rock_hero::editor::ui
