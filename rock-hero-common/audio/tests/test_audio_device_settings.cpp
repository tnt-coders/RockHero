#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <rock_hero/common/audio/audio_device_settings.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr char g_asio_type_name[] = "ASIO";
constexpr char g_input_a[] = "Input A";
constexpr char g_input_b[] = "Input B";
constexpr char g_output_a[] = "Output A";
constexpr char g_output_b[] = "Output B";
constexpr char g_open_output_b_error[] = "Could not open Output B";

// Minimal configuration port around a real JUCE device manager so tests exercise the public
// AudioDeviceSettings service rather than private policy helpers.
class FakeAudioDeviceConfiguration final : public IAudioDeviceConfiguration
{
public:
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return device_manager;
    }

    [[nodiscard]] AudioDeviceStatus currentDeviceStatus() const override
    {
        return {};
    }

    void addListener(IAudioDeviceConfiguration::Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    void removeListener(IAudioDeviceConfiguration::Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    void notifyChanged()
    {
        for (auto* listener : listeners)
        {
            listener->onAudioDeviceConfigurationChanged();
        }
    }

    juce::AudioDeviceManager device_manager{};
    std::vector<IAudioDeviceConfiguration::Listener*> listeners{};
};

// Fake device with deterministic channel, sample-rate, buffer-size, and open-failure behavior.
class MockAudioDevice final : public juce::AudioIODevice
{
public:
    MockAudioDevice(
        juce::String type_name, juce::String output_name, juce::String input_name,
        juce::String open_error)
        : juce::AudioIODevice(
              output_name.isNotEmpty() ? output_name : input_name, std::move(type_name))
        , m_open_error(std::move(open_error))
    {}

    [[nodiscard]] juce::StringArray getInputChannelNames() override
    {
        return {"Input 1", "Input 2"};
    }

    [[nodiscard]] juce::StringArray getOutputChannelNames() override
    {
        return {"Output 1", "Output 2", "Output 3", "Output 4"};
    }

    [[nodiscard]] juce::Array<double> getAvailableSampleRates() override
    {
        return {44100.0, 48000.0, 96000.0};
    }

    [[nodiscard]] juce::Array<int> getAvailableBufferSizes() override
    {
        return {128, 256};
    }

    [[nodiscard]] int getDefaultBufferSize() override
    {
        return 128;
    }

    [[nodiscard]] juce::String open(
        const juce::BigInteger& input_channels, const juce::BigInteger& output_channels,
        double sample_rate, int buffer_size_samples) override
    {
        if (m_open_error.isNotEmpty())
        {
            m_last_error = m_open_error;
            return m_open_error;
        }

        m_input_channels = input_channels;
        m_output_channels = output_channels;
        m_sample_rate = sample_rate;
        m_buffer_size = buffer_size_samples;
        m_is_open = true;
        m_last_error.clear();
        return {};
    }

    void close() override
    {
        m_is_open = false;
    }

    [[nodiscard]] bool isOpen() override
    {
        return m_is_open;
    }

    void start(juce::AudioIODeviceCallback* callback) override
    {
        m_callback = callback;
        if (m_callback != nullptr)
        {
            m_callback->audioDeviceAboutToStart(this);
        }
        m_is_playing = true;
    }

    void stop() override
    {
        if (m_is_playing && m_callback != nullptr)
        {
            m_callback->audioDeviceStopped();
        }

        m_is_playing = false;
        m_callback = nullptr;
    }

    [[nodiscard]] bool isPlaying() override
    {
        return m_is_playing;
    }

    [[nodiscard]] juce::String getLastError() override
    {
        return m_last_error;
    }

    [[nodiscard]] int getCurrentBufferSizeSamples() override
    {
        return m_buffer_size;
    }

    [[nodiscard]] double getCurrentSampleRate() override
    {
        return m_sample_rate;
    }

    [[nodiscard]] int getCurrentBitDepth() override
    {
        return 24;
    }

    [[nodiscard]] juce::BigInteger getActiveOutputChannels() const override
    {
        return m_output_channels;
    }

    [[nodiscard]] juce::BigInteger getActiveInputChannels() const override
    {
        return m_input_channels;
    }

    [[nodiscard]] int getOutputLatencyInSamples() override
    {
        return 0;
    }

    [[nodiscard]] int getInputLatencyInSamples() override
    {
        return 0;
    }

private:
    juce::String m_open_error;
    juce::String m_last_error;
    juce::AudioIODeviceCallback* m_callback{nullptr};
    juce::BigInteger m_input_channels;
    juce::BigInteger m_output_channels;
    double m_sample_rate{0.0};
    int m_buffer_size{128};
    bool m_is_open{false};
    bool m_is_playing{false};
};

// Fake device type used to exercise ordering and route staging through JUCE's real manager API.
class MockAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit MockAudioDeviceType(juce::String type_name, juce::StringArray failing_outputs = {})
        : juce::AudioIODeviceType(std::move(type_name))
        , m_failing_outputs(std::move(failing_outputs))
    {}

    void scanForDevices() override
    {
        ++scan_call_count;
    }

    [[nodiscard]] juce::StringArray getDeviceNames(bool want_input_names) const override
    {
        return want_input_names ? juce::StringArray{g_input_a, g_input_b}
                                : juce::StringArray{g_output_a, g_output_b};
    }

    [[nodiscard]] int getDefaultDeviceIndex(bool /*for_input*/) const override
    {
        return 0;
    }

    [[nodiscard]] int getIndexOfDevice(
        juce::AudioIODevice* device, bool want_input_names) const override
    {
        if (device == nullptr)
        {
            return -1;
        }

        return getDeviceNames(want_input_names).indexOf(device->getName());
    }

    [[nodiscard]] bool hasSeparateInputsAndOutputs() const override
    {
        return true;
    }

    [[nodiscard]] juce::AudioIODevice* createDevice(
        const juce::String& output_device_name, const juce::String& input_device_name) override
    {
        if (!getDeviceNames(true).contains(input_device_name) ||
            !getDeviceNames(false).contains(output_device_name))
        {
            return nullptr;
        }

        const juce::String open_error = m_failing_outputs.contains(output_device_name)
                                            ? juce::String{g_open_output_b_error}
                                            : juce::String{};
        return new MockAudioDevice{
            getTypeName(), output_device_name, input_device_name, open_error
        };
    }

    // Public test observation for scan-cache behavior exercised through AudioDeviceSettings.
    int scan_call_count{};

private:
    juce::StringArray m_failing_outputs;
};

// Listener fake that counts public AudioDeviceSettings notifications.
class FakeAudioDeviceSettingsListener final : public IAudioDeviceSettings::Listener
{
public:
    void onAudioDeviceSettingsChanged() override
    {
        ++call_count;
    }

    int call_count{};
};

[[nodiscard]] juce::AudioDeviceManager::AudioDeviceSetup initialRouteSetup()
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = g_input_a;
    setup.outputDeviceName = g_output_a;
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setBit(0);
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setBit(0);
    setup.outputChannels.setBit(1);
    setup.sampleRate = 48000.0;
    setup.bufferSize = 128;
    return setup;
}

MockAudioDeviceType& addMockAudioType(
    juce::AudioDeviceManager& manager, juce::String type_name,
    juce::StringArray failing_outputs = {})
{
    auto device_type =
        std::make_unique<MockAudioDeviceType>(std::move(type_name), std::move(failing_outputs));
    auto& result = *device_type;
    manager.addAudioDeviceType(std::move(device_type));
    return result;
}

void openInitialRoute(
    FakeAudioDeviceConfiguration& audio_devices, juce::StringArray failing_outputs = {})
{
    addMockAudioType(audio_devices.device_manager, g_asio_type_name, std::move(failing_outputs));

    const juce::String error =
        audio_devices.device_manager.setAudioDeviceSetup(initialRouteSetup(), true);
    REQUIRE(error.isEmpty());
}

} // namespace

// Preferred backend ordering is now observed through the public settings state.
TEST_CASE("AudioDeviceSettings orders Windows audio systems", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    addMockAudioType(audio_devices.device_manager, "DirectSound");
    addMockAudioType(audio_devices.device_manager, "WaveOut");
    addMockAudioType(audio_devices.device_manager, "Windows Audio");
    addMockAudioType(audio_devices.device_manager, "Windows Audio (Exclusive Mode)");
    addMockAudioType(audio_devices.device_manager, "Windows Audio (Low Latency Mode)");
    addMockAudioType(audio_devices.device_manager, "ASIO");

    AudioDeviceSettings settings{audio_devices};
    const AudioDeviceSettingsState state = settings.state();

    CHECK(
        state.audio_systems == std::vector<std::string>{
                                   "ASIO",
                                   "Windows Audio (Exclusive Mode)",
                                   "Windows Audio (Low Latency Mode)",
                                   "Windows Audio",
                                   "DirectSound",
                                   "WaveOut",
                               });
}

// Initial state derives route, channel, sample-rate, and buffer-size IDs from the active backend
// and immediately closes the audio device so the user can edit hardware settings without holding
// it.
TEST_CASE("AudioDeviceSettings initializes active route state", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);

    AudioDeviceSettings settings{audio_devices};
    const AudioDeviceSettingsState state = settings.state();

    CHECK(state.selected_audio_system_id == 1);
    CHECK(state.uses_separate_input_output_devices);
    CHECK(state.input_devices == std::vector<std::string>{g_input_a, g_input_b});
    CHECK(state.output_devices == std::vector<std::string>{g_output_a, g_output_b});
    CHECK(state.selected_input_device_id == 1);
    CHECK(state.selected_output_device_id == 1);
    CHECK(state.input_channels == std::vector<std::string>{"Input 1", "Input 2"});
    REQUIRE(state.stereo_output_pairs.size() == 2);
    CHECK(state.stereo_output_pairs[0].label == "Output 1 + Output 2");
    CHECK(state.stereo_output_pairs[1].label == "Output 3 + Output 4");
    CHECK(state.selected_sample_rate_id == 2);
    CHECK(state.selected_buffer_size_id == 1);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// Selection changes update staged state only; the active device manager changes on apply().
TEST_CASE("AudioDeviceSettings stages output device", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    AudioDeviceSettings settings{audio_devices};
    settings.selectOutputDevice(2);

    CHECK(settings.state().selected_output_device_id == 2);
    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
}

// OK/apply commits the staged route through the public settings service. The device should be
// open with the staged setup after apply() returns successfully.
TEST_CASE("AudioDeviceSettings applies staged route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    AudioDeviceSettings settings{audio_devices};
    settings.selectOutputDevice(2);
    const auto result = settings.apply();

    const auto applied_setup = audio_devices.device_manager.getAudioDeviceSetup();
    CHECK(result.has_value());
    CHECK(applied_setup.inputDeviceName == g_input_a);
    CHECK(applied_setup.outputDeviceName == g_output_b);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
}

// Switching audio systems should avoid JUCE's fixed 1.5 second type-switch sleep.
TEST_CASE("AudioDeviceSettings avoids JUCE type-switch delay", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    addMockAudioType(audio_devices.device_manager, "Windows Audio");

    AudioDeviceSettings settings{audio_devices};
    settings.selectAudioSystem(2);

    const auto started_at = std::chrono::steady_clock::now();
    const auto result = settings.apply();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started_at)
                                .count();

    CHECK(result.has_value());
    CHECK(audio_devices.device_manager.getCurrentAudioDeviceType() == "Windows Audio");
    CHECK(elapsed_ms < 1000);
}

// JUCE delivers route-change broadcasts asynchronously, so the configuration listener can fire
// after apply() returns. The error message that apply() set must survive that refresh so the
// user-facing label does not silently clear a just-reported failure.
TEST_CASE(
    "AudioDeviceSettings preserves error across backend refresh", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices, juce::StringArray{g_output_b});

    AudioDeviceSettings settings{audio_devices};
    settings.selectOutputDevice(2);
    const auto result = settings.apply();
    REQUIRE_FALSE(result.has_value());
    REQUIRE(settings.state().error_message == g_open_output_b_error);

    audio_devices.notifyChanged();

    CHECK(settings.state().error_message == g_open_output_b_error);
}

// A backend notification can mean a device was added or removed under the same audio system.
TEST_CASE("AudioDeviceSettings rescans same backend refresh", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    auto& audio_type = addMockAudioType(audio_devices.device_manager, g_asio_type_name);

    AudioDeviceSettings settings{audio_devices};
    const int initial_scan_count = audio_type.scan_call_count;
    REQUIRE(initial_scan_count > 0);

    audio_devices.notifyChanged();

    CHECK(audio_type.scan_call_count == initial_scan_count + 1);
}

// Apply failures return a typed error and leave the backend closed.
TEST_CASE("AudioDeviceSettings leaves failed apply closed", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices, juce::StringArray{g_output_b});

    AudioDeviceSettings settings{audio_devices};
    settings.selectOutputDevice(2);
    const auto result = settings.apply();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioDeviceSettingsErrorCode::ApplyFailed);
    CHECK(result.error().message == g_open_output_b_error);
    CHECK(settings.state().error_message == g_open_output_b_error);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);

    settings.cancel();

    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// Cancel reopens the device that was open when settings construction began, regardless of staged
// edits.
TEST_CASE("AudioDeviceSettings cancels staged route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    settings.selectOutputDevice(2);

    settings.cancel();

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
}

// Cancel leaves audio closed when the settings edit was opened from an already-closed route.
TEST_CASE("AudioDeviceSettings cancel preserves closed route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    audio_devices.device_manager.closeAudioDevice();
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    settings.selectOutputDevice(2);

    settings.cancel();

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// Destruction without an explicit cancel restores the device that was open at construction.
// This is the native-window-close backstop.
TEST_CASE("AudioDeviceSettings restores device on destruction", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    {
        AudioDeviceSettings settings{audio_devices};
        settings.selectOutputDevice(2);
        REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    }

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
}

// Destruction also leaves audio closed when there was no open route to restore.
TEST_CASE(
    "AudioDeviceSettings destruction preserves closed route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    audio_devices.device_manager.closeAudioDevice();
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    {
        AudioDeviceSettings settings{audio_devices};
        settings.selectOutputDevice(2);
        REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    }

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// A closed preview device with no selected sample rate still defaults through the public state.
TEST_CASE("AudioDeviceSettings defaults staged sample rate", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    addMockAudioType(audio_devices.device_manager, g_asio_type_name);

    AudioDeviceSettings settings{audio_devices};
    const AudioDeviceSettingsState state = settings.state();

    CHECK(state.sample_rates == std::vector<double>{44100.0, 48000.0, 96000.0});
    CHECK(state.selected_sample_rate_id == 2);
}

// Switching audio systems should reset format selections to their defaults so a stale staged
// rate or buffer size from the previous backend does not apply to the new one.
TEST_CASE("AudioDeviceSettings resets format on system change", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    addMockAudioType(audio_devices.device_manager, g_asio_type_name);
    addMockAudioType(audio_devices.device_manager, "Windows Audio");

    AudioDeviceSettings settings{audio_devices};
    settings.selectSampleRate(3);
    settings.selectBufferSize(2);
    REQUIRE(settings.state().selected_sample_rate_id == 3);
    REQUIRE(settings.state().selected_buffer_size_id == 2);

    settings.selectAudioSystem(2);

    const AudioDeviceSettingsState state = settings.state();
    CHECK(state.selected_audio_system_id == 2);
    CHECK(state.selected_sample_rate_id == 2);
    CHECK(state.selected_buffer_size_id == 1);
}

// The public settings listener refreshes when the underlying configuration port broadcasts.
TEST_CASE("AudioDeviceSettings forwards backend refresh", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    AudioDeviceSettings settings{audio_devices};
    FakeAudioDeviceSettingsListener listener;
    settings.addListener(listener);

    audio_devices.notifyChanged();

    CHECK(listener.call_count == 1);
    settings.removeListener(listener);
}

} // namespace rock_hero::common::audio
