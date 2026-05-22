#include "audio_device_settings_view.h"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr char g_audio_system_name[] = "ASIO";
constexpr char g_input_a[] = "Input A";
constexpr char g_input_b[] = "Input B";
constexpr char g_output_a[] = "Output A";
constexpr char g_output_b[] = "Output B";
constexpr char g_open_output_b_error[] = "Could not open Output B";

// Finds direct children by component ID so tests observe the same controls users interact with.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredChild(juce::Component& parent, const juce::String& id)
{
    auto* child = parent.findChildWithID(id);
    REQUIRE(child != nullptr);

    auto* typed_child = dynamic_cast<ComponentType*>(child);
    REQUIRE(typed_child != nullptr);
    return *typed_child;
}

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
        return {"Output 1", "Output 2"};
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
    double m_sample_rate{48000.0};
    int m_buffer_size{128};
    bool m_is_open{false};
    bool m_is_playing{false};
};

class MockAudioDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit MockAudioDeviceType(juce::StringArray failing_outputs = {})
        : juce::AudioIODeviceType(g_audio_system_name)
        , m_failing_outputs(std::move(failing_outputs))
    {}

    void scanForDevices() override
    {}

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

private:
    juce::StringArray m_failing_outputs;
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

void openInitialRoute(juce::AudioDeviceManager& manager, juce::StringArray failing_outputs = {})
{
    manager.addAudioDeviceType(std::make_unique<MockAudioDeviceType>(std::move(failing_outputs)));

    const juce::String error = manager.setAudioDeviceSetup(initialRouteSetup(), true);
    REQUIRE(error.isEmpty());
}

void stageOutputB(AudioDeviceSettingsView& view)
{
    auto& output_combo = findRequiredChild<juce::ComboBox>(view, "audio_settings_output_device");
    REQUIRE(output_combo.getNumItems() == 2);

    output_combo.setSelectedId(2, juce::sendNotificationSync);

    CHECK(output_combo.getText() == g_output_b);
}

void clickTextButton(AudioDeviceSettingsView& view, const juce::String& component_id)
{
    auto& button = findRequiredChild<juce::TextButton>(view, component_id);
    REQUIRE(button.onClick);
    button.onClick();
}

} // namespace

TEST_CASE("AudioDeviceSettingsView keeps staged route on cancel", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    juce::AudioDeviceManager manager;
    openInitialRoute(manager);
    const auto initial_setup = manager.getAudioDeviceSetup();

    AudioDeviceSettingsView view{manager};
    stageOutputB(view);
    CHECK(manager.getAudioDeviceSetup() == initial_setup);

    clickTextButton(view, "audio_settings_cancel_button");

    CHECK(manager.getAudioDeviceSetup() == initial_setup);
}

TEST_CASE("AudioDeviceSettingsView applies staged route on OK", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    juce::AudioDeviceManager manager;
    openInitialRoute(manager);

    AudioDeviceSettingsView view{manager};
    stageOutputB(view);

    clickTextButton(view, "audio_settings_ok_button");

    const auto applied_setup = manager.getAudioDeviceSetup();
    CHECK(applied_setup.inputDeviceName == g_input_a);
    CHECK(applied_setup.outputDeviceName == g_output_b);
}

TEST_CASE("AudioDeviceSettingsView rolls back failed apply", "[ui][audio-device-settings]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    juce::AudioDeviceManager manager;
    openInitialRoute(manager, juce::StringArray{g_output_b});
    const auto initial_setup = manager.getAudioDeviceSetup();

    AudioDeviceSettingsView view{manager};
    stageOutputB(view);

    clickTextButton(view, "audio_settings_ok_button");

    const auto restored_setup = manager.getAudioDeviceSetup();
    CHECK(restored_setup == initial_setup);

    const auto& error_label = findRequiredChild<juce::Label>(view, "audio_settings_error");
    CHECK(error_label.getText() == g_open_output_b_error);
}

} // namespace rock_hero::editor::ui
