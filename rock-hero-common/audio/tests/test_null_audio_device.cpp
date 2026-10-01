#include "device/null_audio_device.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace rock_hero::common::audio
{

namespace
{

// Records the device's lifecycle calls and checks every block it is handed is silent.
class CountingCallback final : public juce::AudioIODeviceCallback
{
public:
    void audioDeviceIOCallbackWithContext(
        const float* const* /*input_channel_data*/, int /*num_input_channels*/,
        float* const* output_channel_data, int num_output_channels, int num_samples,
        const juce::AudioIODeviceCallbackContext& /*context*/) override
    {
        for (int channel = 0; channel < num_output_channels; ++channel)
        {
            for (int sample = 0; sample < num_samples; ++sample)
            {
                if (std::is_neq(output_channel_data[channel][sample] <=> 0.0f))
                {
                    m_silent = false;
                }
            }
        }
        m_rendered.signal();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* /*device*/) override
    {
        m_started = true;
    }

    void audioDeviceStopped() override
    {
        m_stopped = true;
    }

    [[nodiscard]] bool waitForCallback() const
    {
        return m_rendered.wait(5000.0);
    }

    [[nodiscard]] bool started() const noexcept
    {
        return m_started;
    }

    [[nodiscard]] bool stopped() const noexcept
    {
        return m_stopped;
    }

    [[nodiscard]] bool silent() const noexcept
    {
        return m_silent;
    }

private:
    std::atomic<bool> m_silent{true};
    bool m_started{false};
    bool m_stopped{false};
    juce::WaitableEvent m_rendered;
};

} // namespace

// Each block is due one period after the last, measured from the run's start, so the long-run rate
// is exact: a wake-up on time waits a full period, an early one waits the remainder, and one late
// by less than a period renders at once and keeps the schedule.
TEST_CASE("Silent device clock paces blocks from the run's start", "[audio][null-device]")
{
    NullAudioDeviceClock clock{.start_ms = 1000.0, .blocks = 0, .period_ms = 10.0};

    CHECK_THAT(clock.advance(1000.0), Catch::Matchers::WithinAbs(10.0, 1e-9));
    CHECK_THAT(clock.advance(1014.0), Catch::Matchers::WithinAbs(6.0, 1e-9));
    CHECK_THAT(clock.advance(1035.0), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK(clock.blocks == 3);
    CHECK_THAT(clock.start_ms, Catch::Matchers::WithinAbs(1000.0, 1e-9));
}

// A clock more than one period behind restarts at the present rather than rendering its backlog in
// a burst: the lost time is dropped, as a real device under-run drops it.
TEST_CASE("Silent device clock restarts after falling a period behind", "[audio][null-device]")
{
    NullAudioDeviceClock clock{.start_ms = 1000.0, .blocks = 0, .period_ms = 10.0};

    CHECK_THAT(clock.advance(1500.0), Catch::Matchers::WithinAbs(0.0, 1e-9));
    CHECK(clock.blocks == 0);
    CHECK_THAT(clock.start_ms, Catch::Matchers::WithinAbs(1500.0, 1e-9));
    CHECK_THAT(clock.advance(1500.0), Catch::Matchers::WithinAbs(10.0, 1e-9));
}

// The silent device opens output-only, renders silence on its own thread, and joins that thread
// before announcing the stop.
TEST_CASE("Silent device renders silence until stopped", "[audio][null-device]")
{
    const std::unique_ptr<juce::AudioIODeviceType> type = createNullAudioDeviceType();
    REQUIRE(isNullAudioDeviceType(*type));
    CHECK(type->getDeviceNames(true).isEmpty());
    REQUIRE(type->getDeviceNames(false) == juce::StringArray{g_null_audio_device_name});

    const std::unique_ptr<juce::AudioIODevice> device{type->createDevice(
        g_null_audio_device_name, {})};
    REQUIRE(device != nullptr);
    CHECK(isNullAudioDevice(device.get()));

    juce::BigInteger outputs;
    outputs.setRange(0, 2, true);
    REQUIRE(device->open({}, outputs, 48000.0, 512).isEmpty());
    CHECK(device->getActiveInputChannels().isZero());

    CountingCallback callback;
    device->start(&callback);
    CHECK(callback.started());
    REQUIRE(callback.waitForCallback());

    device->stop();
    CHECK(callback.stopped());
    CHECK(callback.silent());
    CHECK_FALSE(device->isPlaying());
}

} // namespace rock_hero::common::audio
