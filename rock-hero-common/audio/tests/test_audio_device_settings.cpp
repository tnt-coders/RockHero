#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/device/audio_device_settings.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <string>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr const char* g_asio_type_name = "ASIO";
constexpr const char* g_input_a = "Input A";
constexpr const char* g_input_b = "Input B";
constexpr const char* g_output_a = "Output A";
constexpr const char* g_output_b = "Output B";
constexpr const char* g_open_output_b_error = "Could not open Output B";

// Fake device with deterministic channel, sample-rate, buffer-size, and open-failure behavior.
class MockAudioDevice final : public juce::AudioIODevice
{
public:
    MockAudioDevice(
        const juce::String& type_name, const juce::String& output_name,
        const juce::String& input_name, juce::String open_error, bool has_control_panel,
        bool show_control_panel_result, int* control_panel_call_count,
        juce::String driver_init_error = {})
        : juce::AudioIODevice(output_name.isNotEmpty() ? output_name : input_name, type_name)
        , m_open_error(std::move(open_error))
        , m_has_control_panel(has_control_panel)
        , m_show_control_panel_result(show_control_panel_result)
        , m_control_panel_call_count(control_panel_call_count)
        // Mirrors ASIO's construction-time driver init: a failed init (hardware unplugged, or the
        // device held by another application) leaves getLastError() non-empty from birth.
        , m_last_error(std::move(driver_init_error))
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

    [[nodiscard]] bool hasControlPanel() const override
    {
        return m_has_control_panel;
    }

    [[nodiscard]] bool showControlPanel() override
    {
        if (m_control_panel_call_count != nullptr)
        {
            *m_control_panel_call_count += 1;
        }
        return m_show_control_panel_result;
    }

private:
    juce::String m_open_error;
    bool m_has_control_panel{false};
    bool m_show_control_panel_result{false};
    int* m_control_panel_call_count{nullptr};
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
    explicit MockAudioDeviceType(
        const juce::String& type_name, juce::StringArray failing_outputs = {},
        bool has_control_panel = false, bool show_control_panel_result = true,
        juce::StringArray driver_init_failing_outputs = {})
        : juce::AudioIODeviceType(type_name)
        , m_failing_outputs(std::move(failing_outputs))
        , m_driver_init_failing_outputs(std::move(driver_init_failing_outputs))
        , m_has_control_panel(has_control_panel)
        , m_show_control_panel_result(show_control_panel_result)
    {}

    void scanForDevices() override
    {
        ++m_scan_call_count;
    }

    [[nodiscard]] juce::StringArray getDeviceNames(bool want_input_names) const override
    {
        return want_input_names ? m_input_device_names : m_output_device_names;
    }

    // Replaces the advertised device lists so tests can simulate hot-plug arrivals and removals.
    void setDeviceNames(juce::StringArray input_names, juce::StringArray output_names)
    {
        m_input_device_names = std::move(input_names);
        m_output_device_names = std::move(output_names);
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
        const juce::String driver_init_error =
            m_driver_init_failing_outputs.contains(output_device_name) ? m_driver_init_error_text
                                                                       : juce::String{};
        auto device = std::make_unique<MockAudioDevice>(
            getTypeName(),
            output_device_name,
            input_device_name,
            open_error,
            m_has_control_panel,
            m_show_control_panel_result,
            &m_control_panel_call_count,
            driver_init_error);

        // JUCE's AudioIODeviceType factory transfers ownership through the raw pointer return.
        return device.release(); // NOLINT(cppcoreguidelines-owning-memory)
    }

    // Configures the error text a driver-init-failing device reports from construction, so tests
    // can exercise both a backend-authored detail and JUCE's no-message placeholder.
    void setDriverInitErrorText(juce::String error_text)
    {
        m_driver_init_error_text = std::move(error_text);
    }

    // Test observation for scan-cache behavior exercised through AudioDeviceSettings.
    [[nodiscard]] int scanCallCount() const noexcept
    {
        return m_scan_call_count;
    }

    // Test observation for backend control-panel dispatches.
    [[nodiscard]] int controlPanelCallCount() const noexcept
    {
        return m_control_panel_call_count;
    }

private:
    juce::StringArray m_input_device_names{g_input_a, g_input_b};
    juce::StringArray m_output_device_names{g_output_a, g_output_b};
    juce::StringArray m_failing_outputs;
    juce::StringArray m_driver_init_failing_outputs;
    juce::String m_driver_init_error_text{"Can't detect asio channels"};
    bool m_has_control_panel{false};
    bool m_show_control_panel_result{false};
    int m_scan_call_count{};
    int m_control_panel_call_count{};
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
    juce::AudioDeviceManager& manager, const juce::String& type_name,
    juce::StringArray failing_outputs = {}, bool has_control_panel = false,
    bool show_control_panel_result = true, juce::StringArray driver_init_failing_outputs = {})
{
    auto device_type = std::make_unique<MockAudioDeviceType>(
        type_name,
        std::move(failing_outputs),
        has_control_panel,
        show_control_panel_result,
        std::move(driver_init_failing_outputs));
    auto& result = *device_type;
    manager.addAudioDeviceType(std::move(device_type));
    return result;
}

void openInitialRoute(
    testing::ConfigurableAudioDeviceConfiguration& audio_devices,
    juce::StringArray failing_outputs = {})
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    addMockAudioType(audio_devices.device_manager, "DirectSound");
    addMockAudioType(audio_devices.device_manager, "WaveOut");
    addMockAudioType(audio_devices.device_manager, "Windows Audio");
    addMockAudioType(audio_devices.device_manager, "Windows Audio (Exclusive Mode)");
    addMockAudioType(audio_devices.device_manager, "Windows Audio (Low Latency Mode)");
    addMockAudioType(audio_devices.device_manager, "ASIO");

    const AudioDeviceSettings settings{audio_devices};
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);

    const AudioDeviceSettings settings{audio_devices};
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    auto& audio_type = addMockAudioType(audio_devices.device_manager, g_asio_type_name);

    const AudioDeviceSettings settings{audio_devices};
    const int initial_scan_count = audio_type.scanCallCount();
    REQUIRE(initial_scan_count > 0);

    audio_devices.notifyChanged();

    CHECK(audio_type.scanCallCount() == initial_scan_count + 1);
}

// Apply failures return a typed error and leave the backend closed.
TEST_CASE("AudioDeviceSettings leaves failed apply closed", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices, juce::StringArray{g_output_b});

    AudioDeviceSettings settings{audio_devices};
    settings.selectOutputDevice(2);
    const auto result = settings.apply();

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == AudioDeviceSettingsErrorCode::ApplyFailed);
    CHECK(result.error().message == g_open_output_b_error);
    CHECK(settings.state().error_message == g_open_output_b_error);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);

    REQUIRE(settings.cancel().has_value());

    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// Cancel reopens the device that was open when settings construction began, regardless of staged
// edits.
TEST_CASE("AudioDeviceSettings cancels staged route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    settings.selectOutputDevice(2);

    REQUIRE(settings.cancel().has_value());

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
}

// Cancel leaves audio closed when the settings edit was opened from an already-closed route.
TEST_CASE("AudioDeviceSettings cancel preserves closed route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    audio_devices.device_manager.closeAudioDevice();
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    settings.selectOutputDevice(2);

    REQUIRE(settings.cancel().has_value());

    CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// Destruction without an explicit cancel restores the device that was open at construction.
// This is the native-window-close backstop.
TEST_CASE("AudioDeviceSettings restores device on destruction", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    addMockAudioType(audio_devices.device_manager, g_asio_type_name);

    const AudioDeviceSettings settings{audio_devices};
    const AudioDeviceSettingsState state = settings.state();

    CHECK(state.sample_rates == std::vector<double>{44100.0, 48000.0, 96000.0});
    CHECK(state.selected_sample_rate_id == 2);
}

// showControlPanel()'s bool is a dwell-time reload heuristic (ASIO returns true only when the user
// lingered in the panel long enough to justify reloading buffer sizes), not a success signal. As
// long as the driver exposes a control panel, opening it succeeds even when showControlPanel()
// returns false, so no spurious failure surfaces.
TEST_CASE(
    "AudioDeviceSettings opens control panel regardless of dwell-time return",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    const auto& audio_type = addMockAudioType(
        audio_devices.device_manager, g_asio_type_name, juce::StringArray{}, true, false);

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(settings.state().control_panel_supported);
    REQUIRE_FALSE(settings.state().staged_device_error.has_value());

    const auto opened = settings.openControlPanel();

    CHECK(opened.has_value());
    CHECK(audio_type.controlPanelCallCount() == 1);
    CHECK(settings.state().error_message.empty());
}

// A backend that exposes no control panel is gated by hasControlPanel() (the only reliable
// capability signal): the request returns the unavailable error without dispatching to the driver.
TEST_CASE(
    "AudioDeviceSettings reports control panel unavailable without a panel",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    const auto& audio_type = addMockAudioType(
        audio_devices.device_manager, g_asio_type_name, juce::StringArray{}, false);

    AudioDeviceSettings settings{audio_devices};
    REQUIRE_FALSE(settings.state().control_panel_supported);

    const auto opened = settings.openControlPanel();

    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == AudioDeviceSettingsErrorCode::ControlPanelUnavailable);
    CHECK(settings.state().error_message == opened.error().message);
    CHECK(audio_type.controlPanelCallCount() == 0);
}

// A driver whose construction-time init failed (hardware unplugged, or the device held by another
// application) still claims a control panel, but showing it would silently do nothing (verified
// against JUCE's ASIO backend: showControlPanel() no-ops without an initialized driver object).
// The staged device therefore reports unavailable, and the panel request errors instead of
// dispatching a no-op to the driver.
TEST_CASE(
    "AudioDeviceSettings reports control panel unavailable when the driver init failed",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    const auto& audio_type = addMockAudioType(
        audio_devices.device_manager,
        g_asio_type_name,
        juce::StringArray{},
        true,
        true,
        juce::StringArray{g_output_a});

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(settings.state().control_panel_supported);
    REQUIRE(settings.state().staged_device_error.has_value());
    CHECK(
        settings.state().staged_device_error.value_or(std::string{}) ==
        "Can't detect asio channels");

    const auto opened = settings.openControlPanel();

    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().code == AudioDeviceSettingsErrorCode::ControlPanelUnavailable);
    CHECK(settings.state().error_message == opened.error().message);
    CHECK(audio_type.controlPanelCallCount() == 0);
}

// Applying a route whose driver init failed reports the one canonical unavailable message rather
// than the backend's raw setup text (for ASIO literally "Driver failed to initialise"), so the OK
// failure reads identically to the window's standing unavailable notice. Other apply failures keep
// the backend's specific diagnostic.
TEST_CASE(
    "AudioDeviceSettings apply adopts an unavailable route as closed",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    addMockAudioType(
        audio_devices.device_manager,
        g_asio_type_name,
        juce::StringArray{},
        true,
        true,
        juce::StringArray{g_output_a});

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(settings.state().staged_device_error.has_value());

    const auto applied = settings.apply();

    // The designed no-fallback outcome: the route is adopted with the device closed, OK closes the
    // settings window, and the editor status shows [audio device closed]. The standing notice
    // (staged_device_error) already explains the condition, so no operation error is raised.
    CHECK(applied.has_value());
    CHECK(settings.state().error_message.empty());
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
}

// JUCE substitutes the pinned placeholder "Driver failed to initialise" when a failed driver init
// supplies no vendor message; it passes through like any other backend text, normalized only to
// American spelling at this boundary.
TEST_CASE(
    "AudioDeviceSettings normalizes the placeholder spelling for a silent driver",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    auto& audio_type = addMockAudioType(
        audio_devices.device_manager,
        g_asio_type_name,
        juce::StringArray{},
        true,
        true,
        juce::StringArray{g_output_a});
    audio_type.setDriverInitErrorText("Driver failed to initialise");

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(settings.state().staged_device_error.has_value());
    // The pinned JUCE placeholder passes through like any other backend text, with the one
    // boundary translation being its spelling (Rock Hero's user-facing text is American English).
    CHECK(
        settings.state().staged_device_error.value_or(std::string{}) ==
        "Driver failed to initialize");

    const auto applied = settings.apply();

    // The silent driver adopts identically: closed route, no operation error.
    CHECK(applied.has_value());
    CHECK(settings.state().error_message.empty());
}

// commit() finishes the edit on whichever route is live at commit time. When nothing opened a
// device while the window was open, that is the captured pre-edit route construction closed for
// staging, so commit reopens it: a plain OK on an untouched window must never leave audio dead.
TEST_CASE("AudioDeviceSettings commit reopens the captured route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);
    const auto initial_setup = audio_devices.device_manager.getAudioDeviceSetup();

    {
        AudioDeviceSettings settings{audio_devices};
        // Construction closes the active device for editing.
        REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);

        const auto committed = settings.commit();
        REQUIRE(committed.has_value());
        CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
        CHECK(audio_devices.device_manager.getAudioDeviceSetup() == initial_setup);
    }

    // The reopen belongs to commit(); destruction must not run a second restore or close.
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
}

// commit() keeps a route opened out of band during the edit -- the editor's live "use game audio
// settings" toggle opens the adopted device while the window is still up -- and must not restore
// the captured pre-edit route over it.
TEST_CASE(
    "AudioDeviceSettings commit keeps the out-of-band route", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);

    juce::AudioDeviceManager::AudioDeviceSetup adopted = initialRouteSetup();
    adopted.inputDeviceName = g_input_b;
    adopted.outputDeviceName = g_output_b;
    REQUIRE(audio_devices.device_manager.setAudioDeviceSetup(adopted, true).isEmpty());

    const auto committed = settings.commit();
    REQUIRE(committed.has_value());
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
    CHECK(
        audio_devices.device_manager.getAudioDeviceSetup().outputDeviceName ==
        juce::String{g_output_b});
}

// An edit can begin on a closed, disconnected device (the startup restore fell back nowhere by
// design), so there is no pending restore. When the hardware returns while the window is open, OK
// is an explicit confirmation of the shown route and must finish with the device open -- the
// restore-pending gate only guards cancel's "don't start audio that was not running".
TEST_CASE(
    "AudioDeviceSettings commit opens the saved route once its device returns",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    MockAudioDeviceType& audio_type =
        addMockAudioType(audio_devices.device_manager, g_asio_type_name);
    REQUIRE(audio_devices.device_manager.setAudioDeviceSetup(initialRouteSetup(), true).isEmpty());

    juce::XmlElement saved{"DEVICESETUP"};
    saved.setAttribute("deviceType", g_asio_type_name);
    saved.setAttribute("audioInputDeviceName", "Input Z");
    saved.setAttribute("audioOutputDeviceName", "Output Z");
    REQUIRE(audio_devices.device_manager.initialise(1, 2, &saved, false).isNotEmpty());

    AudioDeviceSettings settings{audio_devices};
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);

    audio_type.setDeviceNames(
        {g_input_a, g_input_b, "Input Z"}, {g_output_a, g_output_b, "Output Z"});
    audio_devices.notifyChanged();
    REQUIRE_FALSE(settings.state().staged_device_error.has_value());

    const auto committed = settings.commit();
    REQUIRE(committed.has_value());
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() != nullptr);
    CHECK(
        audio_devices.device_manager.getAudioDeviceSetup().outputDeviceName ==
        juce::String{"Output Z"});
}

// When the chosen device is still missing at OK time, the reopen attempt fails as the designed
// no-fallback outcome: commit succeeds so the window closes, the device stays closed, and the
// route remains the user's explicit choice for the next replug or launch.
TEST_CASE(
    "AudioDeviceSettings commit leaves a still-missing device closed",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    juce::XmlElement saved{"DEVICESETUP"};
    saved.setAttribute("deviceType", g_asio_type_name);
    saved.setAttribute("audioInputDeviceName", "Input Z");
    saved.setAttribute("audioOutputDeviceName", "Output Z");
    REQUIRE(audio_devices.device_manager.initialise(1, 2, &saved, false).isNotEmpty());

    AudioDeviceSettings settings{audio_devices};

    const auto committed = settings.commit();
    REQUIRE(committed.has_value());
    CHECK(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    CHECK(settings.state().staged_device_error.has_value());
}

// The settings edit marks route staging for its entire lifetime so the backend's no-fallback
// policy never auto-reopens the saved route under the open settings window; the flag clears on
// destruction so replug handling resumes once the window is gone.
TEST_CASE(
    "AudioDeviceSettings marks route staging while the edit is alive",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    {
        const AudioDeviceSettings settings{audio_devices};
        CHECK(audio_devices.route_staging_active);
    }

    CHECK_FALSE(audio_devices.route_staging_active);
}

// After a failed no-fallback restore of a missing device, JUCE's live setup no longer names the
// user's choice (the failure clears the setup's device names on its way out), so seeding the edit
// from the live setup would snap it to the driver's default device. The user's actual choice
// survives only in the saved state XML: the settings edit must seed from it, keep it selected
// even though it is disconnected, and surface the standing not-found notice instead of showing a
// different device.
TEST_CASE(
    "AudioDeviceSettings seeds a closed edit from the saved route",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    // Restore a saved route whose device is not attached, exactly as startup does: the open fails
    // with no fallback, the device closes, and the live setup's device names are cleared.
    juce::XmlElement saved{"DEVICESETUP"};
    saved.setAttribute("deviceType", g_asio_type_name);
    saved.setAttribute("audioInputDeviceName", "Input Z");
    saved.setAttribute("audioOutputDeviceName", "Output Z");
    const juce::String restore_error = audio_devices.device_manager.initialise(1, 2, &saved, false);
    REQUIRE(restore_error.isNotEmpty());
    REQUIRE(audio_devices.device_manager.getCurrentAudioDevice() == nullptr);
    REQUIRE(audio_devices.device_manager.getAudioDeviceSetup().inputDeviceName.isEmpty());

    const AudioDeviceSettings settings{audio_devices};
    const AudioDeviceSettingsState state = settings.state();

    CHECK(state.input_devices == std::vector<std::string>{g_input_a, g_input_b, "Input Z"});
    CHECK(state.output_devices == std::vector<std::string>{g_output_a, g_output_b, "Output Z"});
    CHECK(state.selected_input_device_id == 3);
    CHECK(state.selected_output_device_id == 3);
    CHECK(
        state.staged_device_error.value_or(std::string{}) ==
        "Audio device not found: Input Z, Output Z");
    CHECK_FALSE(state.control_panel_supported);
}

// A hot-plug arrival of the missing saved device clears the standing notice on the backend's
// refresh broadcast: the kept selection resolves into the rescanned list and the preview device
// becomes creatable again, without the selection ever having been snapped elsewhere.
TEST_CASE(
    "AudioDeviceSettings resolves the missing device when it reappears",
    "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    MockAudioDeviceType& audio_type =
        addMockAudioType(audio_devices.device_manager, g_asio_type_name);
    REQUIRE(audio_devices.device_manager.setAudioDeviceSetup(initialRouteSetup(), true).isEmpty());

    juce::XmlElement saved{"DEVICESETUP"};
    saved.setAttribute("deviceType", g_asio_type_name);
    saved.setAttribute("audioInputDeviceName", "Input Z");
    saved.setAttribute("audioOutputDeviceName", "Output Z");
    REQUIRE(audio_devices.device_manager.initialise(1, 2, &saved, false).isNotEmpty());

    const AudioDeviceSettings settings{audio_devices};
    REQUIRE(settings.state().staged_device_error.has_value());

    audio_type.setDeviceNames(
        {g_input_a, g_input_b, "Input Z"}, {g_output_a, g_output_b, "Output Z"});
    audio_devices.notifyChanged();

    const AudioDeviceSettingsState state = settings.state();
    CHECK_FALSE(state.staged_device_error.has_value());
    CHECK(state.input_devices == std::vector<std::string>{g_input_a, g_input_b, "Input Z"});
    CHECK(state.selected_input_device_id == 3);
    CHECK(state.selected_output_device_id == 3);
    CHECK_FALSE(state.sample_rates.empty());
}

// Switching audio systems should reset format selections to their defaults so a stale staged
// rate or buffer size from the previous backend does not apply to the new one.
TEST_CASE("AudioDeviceSettings resets format on system change", "[audio][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
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
    testing::ConfigurableAudioDeviceConfiguration audio_devices;
    openInitialRoute(audio_devices);

    AudioDeviceSettings settings{audio_devices};
    FakeAudioDeviceSettingsListener listener;
    settings.addListener(listener);

    audio_devices.notifyChanged();

    CHECK(listener.call_count == 1);
    settings.removeListener(listener);
}

} // namespace rock_hero::common::audio
