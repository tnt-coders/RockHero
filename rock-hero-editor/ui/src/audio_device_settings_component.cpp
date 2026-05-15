#include "audio_device_settings_component.h"

#include <algorithm>
#include <cmath>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_preferred_width{620};
constexpr int g_content_inset{16};
constexpr int g_label_width{136};
constexpr int g_row_height{28};
constexpr int g_row_gap{8};
constexpr int g_button_width{96};
constexpr int g_utility_button_width{116};
constexpr int g_error_height{32};
constexpr int g_min_control_rows{6};
constexpr int g_max_dialog_width{1000};
constexpr int g_max_dialog_height{760};

// Returns the vertical space occupied by a visible form row set.
[[nodiscard]] int formRowsHeight(int row_count) noexcept
{
    if (row_count <= 0)
    {
        return 0;
    }

    return (row_count * g_row_height) + ((row_count - 1) * g_row_gap);
}

// Returns a derived height that fits the requested row count without clipping controls.
[[nodiscard]] int dialogHeightForRows(int row_count) noexcept
{
    const int controls_height = formRowsHeight(row_count);
    return (g_content_inset * 2) + controls_height + g_row_gap + g_error_height + g_row_gap +
           g_row_height;
}

// Uses stable one-based ComboBox item IDs for a string list.
void populateStringCombo(
    juce::ComboBox& combo, const juce::StringArray& choices, const juce::String& selected_text,
    const juce::String& empty_text)
{
    combo.clear(juce::dontSendNotification);
    combo.setTextWhenNothingSelected("Select...");
    combo.setTextWhenNoChoicesAvailable(empty_text);

    for (int index = 0; index < choices.size(); ++index)
    {
        combo.addItem(choices[index], index + 1);
    }

    const int selected_index = choices.indexOf(selected_text);
    combo.setSelectedId(selected_index >= 0 ? selected_index + 1 : 0, juce::dontSendNotification);
}

// Returns a readable channel name, falling back to a numbered label when the driver omits one.
[[nodiscard]] juce::String channelName(
    const juce::StringArray& channel_names, int channel_index, const juce::String& fallback_prefix)
{
    if (juce::isPositiveAndBelow(channel_index, channel_names.size()) &&
        channel_names[channel_index].isNotEmpty())
    {
        return channel_names[channel_index];
    }

    return fallback_prefix + " " + juce::String{channel_index + 1};
}

// Formats a stereo output pair as a single app-level route.
[[nodiscard]] juce::String outputPairName(
    const juce::StringArray& channel_names, int left_channel, int right_channel)
{
    return channelName(channel_names, left_channel, "Output") + " + " +
           channelName(channel_names, right_channel, "Output");
}

// Displays rates as whole Hz when possible to avoid noisy decimal text.
[[nodiscard]] juce::String sampleRateText(double sample_rate)
{
    const auto rounded = static_cast<int>(std::lround(sample_rate));
    if (std::abs(sample_rate - static_cast<double>(rounded)) < 0.001)
    {
        return juce::String{rounded} + " Hz";
    }

    return juce::String{sample_rate, 1} + " Hz";
}

// Displays buffer sizes using explicit sample units.
[[nodiscard]] juce::String bufferSizeText(int buffer_size)
{
    return juce::String{buffer_size} + " samples";
}

// Keeps a requested device name when it still exists, otherwise picks the driver's default.
[[nodiscard]] juce::String validOrDefaultDeviceName(
    const juce::String& requested_name, const juce::StringArray& device_names, int default_index)
{
    if (device_names.contains(requested_name))
    {
        return requested_name;
    }

    if (juce::isPositiveAndBelow(default_index, device_names.size()))
    {
        return device_names[default_index];
    }

    return device_names.isEmpty() ? juce::String{} : device_names[0];
}

// Finds the first sample-rate choice that matches the current device rate closely enough.
[[nodiscard]] int selectedSampleRateId(const std::vector<double>& sample_rates, double current_rate)
{
    for (int index = 0; index < static_cast<int>(sample_rates.size()); ++index)
    {
        if (std::abs(sample_rates[static_cast<std::size_t>(index)] - current_rate) < 0.001)
        {
            return index + 1;
        }
    }

    return 0;
}

// Finds the first buffer-size choice that matches the current device buffer size.
[[nodiscard]] int selectedBufferSizeId(const std::vector<int>& buffer_sizes, int current_size)
{
    for (int index = 0; index < static_cast<int>(buffer_sizes.size()); ++index)
    {
        if (buffer_sizes[static_cast<std::size_t>(index)] == current_size)
        {
            return index + 1;
        }
    }

    return 0;
}

// Lays out one label/control row when that row is visible for the current driver type.
void layoutRow(juce::Label& label, juce::Component& control, juce::Rectangle<int>& area) noexcept
{
    if (!label.isVisible() && !control.isVisible())
    {
        return;
    }

    auto row = area.removeFromTop(g_row_height);
    label.setBounds(row.removeFromLeft(std::min(g_label_width, row.getWidth())));
    row.removeFromLeft(std::min(g_row_gap, row.getWidth()));
    control.setBounds(row);
    area.removeFromTop(std::min(g_row_gap, area.getHeight()));
}

} // namespace

// Creates the settings surface and captures the initial state used for Cancel/native close.
AudioDeviceSettingsComponent::AudioDeviceSettingsComponent(juce::AudioDeviceManager& device_manager)
    : m_device_manager(device_manager)
    , m_staged_setup(device_manager.getAudioDeviceSetup())
    , m_staged_device_type(device_manager.getCurrentAudioDeviceType())
{
    configureControls();
    m_device_manager.addChangeListener(this);
    refreshControls();
}

// Disconnects from the device manager without altering the live route on dialog close.
AudioDeviceSettingsComponent::~AudioDeviceSettingsComponent()
{
    m_device_manager.removeChangeListener(this);
}

// Returns a default width wide enough for typical ASIO device and channel names.
int AudioDeviceSettingsComponent::preferredWidth() noexcept
{
    return g_preferred_width;
}

// Returns a derived height from the rows visible for the currently selected driver type.
int AudioDeviceSettingsComponent::preferredContentHeight() const noexcept
{
    const int visible_rows = g_min_control_rows + (m_input_device_combo.isVisible() ? 1 : 0);
    return dialogHeightForRows(visible_rows);
}

// Keeps the route selectors usable without requiring the initial window to be very wide.
int AudioDeviceSettingsComponent::minimumWidth() noexcept
{
    return 520;
}

// Caps dialog resizing at a useful settings-panel width.
int AudioDeviceSettingsComponent::maximumWidth() noexcept
{
    return g_max_dialog_width;
}

// Caps dialog resizing at a useful settings-panel height.
int AudioDeviceSettingsComponent::maximumHeight() noexcept
{
    return g_max_dialog_height;
}

// Positions route rows, status text, utility buttons, and final dialog actions.
void AudioDeviceSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(g_content_inset);
    auto button_row = area.removeFromBottom(g_row_height);

    m_cancel_button.setBounds(button_row.removeFromRight(g_button_width));
    button_row.removeFromRight(std::min(g_row_gap, button_row.getWidth()));
    m_ok_button.setBounds(button_row.removeFromRight(g_button_width));

    m_test_button.setBounds(
        button_row.removeFromLeft(std::min(g_utility_button_width, button_row.getWidth())));
    button_row.removeFromLeft(std::min(g_row_gap, button_row.getWidth()));
    m_control_panel_button.setBounds(
        button_row.removeFromLeft(std::min(g_utility_button_width, button_row.getWidth())));

    area.removeFromBottom(std::min(g_row_gap, area.getHeight()));
    m_error_label.setBounds(area.removeFromBottom(std::min(g_error_height, area.getHeight())));
    area.removeFromBottom(std::min(g_row_gap, area.getHeight()));

    layoutRow(m_device_type_label, m_device_type_combo, area);
    layoutRow(m_device_label, m_device_combo, area);
    layoutRow(m_output_device_label, m_output_device_combo, area);
    layoutRow(m_input_device_label, m_input_device_combo, area);
    layoutRow(m_output_pair_label, m_output_pair_combo, area);
    layoutRow(m_input_channel_label, m_input_channel_combo, area);
    layoutRow(m_sample_rate_label, m_sample_rate_combo, area);
    layoutRow(m_buffer_size_label, m_buffer_size_combo, area);
}

// Refreshes controls after the device manager reports an external route change.
void AudioDeviceSettingsComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &m_device_manager && !m_refreshing_controls)
    {
        refreshControls();
    }
}

// Configures labels, callbacks, and static button presentation.
void AudioDeviceSettingsComponent::configureControls()
{
    setComponentID("audio_device_settings_component");

    m_device_type_label.setText("Audio system", juce::dontSendNotification);
    m_device_label.setText("Device", juce::dontSendNotification);
    m_output_device_label.setText("Output device", juce::dontSendNotification);
    m_input_device_label.setText("Input device", juce::dontSendNotification);
    m_output_pair_label.setText("Output", juce::dontSendNotification);
    m_input_channel_label.setText("Input", juce::dontSendNotification);
    m_sample_rate_label.setText("Sample rate", juce::dontSendNotification);
    m_buffer_size_label.setText("Buffer size", juce::dontSendNotification);

    m_device_type_combo.setComponentID("audio_settings_device_type");
    m_device_combo.setComponentID("audio_settings_device");
    m_input_device_combo.setComponentID("audio_settings_input_device");
    m_output_device_combo.setComponentID("audio_settings_output_device");
    m_input_channel_combo.setComponentID("audio_settings_input_channel");
    m_output_pair_combo.setComponentID("audio_settings_output_pair");
    m_sample_rate_combo.setComponentID("audio_settings_sample_rate");
    m_buffer_size_combo.setComponentID("audio_settings_buffer_size");
    m_error_label.setComponentID("audio_settings_error");
    m_test_button.setComponentID("audio_settings_test_button");
    m_control_panel_button.setComponentID("audio_settings_control_panel_button");
    m_ok_button.setComponentID("audio_settings_ok_button");
    m_cancel_button.setComponentID("audio_settings_cancel_button");

    m_error_label.setColour(juce::Label::textColourId, juce::Colours::lightsalmon);
    m_error_label.setJustificationType(juce::Justification::centredLeft);
    m_test_button.setButtonText("Test Output");
    m_control_panel_button.setButtonText("Control Panel");
    m_ok_button.setButtonText("OK");
    m_cancel_button.setButtonText("Cancel");

    m_device_type_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleDeviceTypeChanged();
        }
    };
    m_device_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleDeviceChanged();
        }
    };
    m_input_device_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleDeviceChanged();
        }
    };
    m_output_device_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleDeviceChanged();
        }
    };
    m_input_channel_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleRouteChanged();
        }
    };
    m_output_pair_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleRouteChanged();
        }
    };
    m_sample_rate_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleAudioFormatChanged();
        }
    };
    m_buffer_size_combo.onChange = [this] {
        if (!m_refreshing_controls)
        {
            handleAudioFormatChanged();
        }
    };
    m_test_button.onClick = [this] { m_device_manager.playTestSound(); };
    m_control_panel_button.onClick = [this] {
        if (auto* device = m_device_manager.getCurrentAudioDevice(); device != nullptr)
        {
            device->showControlPanel();
            refreshControls();
        }
    };
    m_ok_button.onClick = [this] { acceptAndClose(); };
    m_cancel_button.onClick = [this] { cancelAndClose(); };

    addAndMakeVisible(m_device_type_label);
    addAndMakeVisible(m_device_type_combo);
    addAndMakeVisible(m_device_label);
    addAndMakeVisible(m_device_combo);
    addAndMakeVisible(m_input_device_label);
    addAndMakeVisible(m_input_device_combo);
    addAndMakeVisible(m_output_device_label);
    addAndMakeVisible(m_output_device_combo);
    addAndMakeVisible(m_input_channel_label);
    addAndMakeVisible(m_input_channel_combo);
    addAndMakeVisible(m_output_pair_label);
    addAndMakeVisible(m_output_pair_combo);
    addAndMakeVisible(m_sample_rate_label);
    addAndMakeVisible(m_sample_rate_combo);
    addAndMakeVisible(m_buffer_size_label);
    addAndMakeVisible(m_buffer_size_combo);
    addAndMakeVisible(m_error_label);
    addAndMakeVisible(m_test_button);
    addAndMakeVisible(m_control_panel_button);
    addAndMakeVisible(m_ok_button);
    addAndMakeVisible(m_cancel_button);
}

// Rebuilds UI choices from the live manager after each route or hardware change.
void AudioDeviceSettingsComponent::refreshControls()
{
    const juce::ScopedValueSetter<bool> refreshing{m_refreshing_controls, true};
    refreshDeviceTypes();
    refreshDeviceNames();
    m_staged_device = createStagedDevice();
    refreshChannelChoices();
    refreshSampleRateChoices();
    refreshBufferSizeChoices();
    refreshControlEnablement();
    syncDialogHeightToContent();
    resized();
}

// Keeps the native dialog height in sync when the backend switches row shape.
void AudioDeviceSettingsComponent::syncDialogHeightToContent()
{
    const int content_height = preferredContentHeight();

    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
    {
        dialog->setResizeLimits(minimumWidth(), content_height, maximumWidth(), maximumHeight());
    }

    const int content_width = getWidth() > 0 ? getWidth() : preferredWidth();
    setSize(content_width, content_height);
}

// Populates the audio backend type selector from JUCE's registered device types.
void AudioDeviceSettingsComponent::refreshDeviceTypes()
{
    ensureStagedDeviceType();

    const auto& device_types = m_device_manager.getAvailableDeviceTypes();
    juce::StringArray type_names;
    for (const auto* device_type : device_types)
    {
        type_names.add(device_type->getTypeName());
    }

    type_names.sort(true);
    populateStringCombo(
        m_device_type_combo, type_names, m_staged_device_type, "No audio systems found");
}

// Populates either one combined device selector or separate input/output selectors.
void AudioDeviceSettingsComponent::refreshDeviceNames()
{
    ensureStagedDeviceNames();

    auto* type = currentDeviceType();
    const bool separate_devices = currentTypeUsesSeparateDevices();

    m_device_label.setVisible(!separate_devices);
    m_device_combo.setVisible(!separate_devices);
    m_input_device_label.setVisible(separate_devices);
    m_input_device_combo.setVisible(separate_devices);
    m_output_device_label.setVisible(separate_devices);
    m_output_device_combo.setVisible(separate_devices);

    if (type == nullptr)
    {
        populateStringCombo(
            m_device_combo, juce::StringArray{}, juce::String{}, "No devices found");
        populateStringCombo(
            m_input_device_combo, juce::StringArray{}, juce::String{}, "No input devices found");
        populateStringCombo(
            m_output_device_combo, juce::StringArray{}, juce::String{}, "No output devices found");
        return;
    }

    type->scanForDevices();
    if (separate_devices)
    {
        populateStringCombo(
            m_input_device_combo,
            type->getDeviceNames(true),
            m_staged_setup.inputDeviceName,
            "No input devices found");
        populateStringCombo(
            m_output_device_combo,
            type->getDeviceNames(false),
            m_staged_setup.outputDeviceName,
            "No output devices found");
        return;
    }

    auto device_names = type->getDeviceNames(false);
    if (device_names.isEmpty())
    {
        device_names = type->getDeviceNames(true);
    }

    const juce::String selected_device = m_staged_setup.outputDeviceName.isNotEmpty()
                                             ? m_staged_setup.outputDeviceName
                                             : m_staged_setup.inputDeviceName;
    populateStringCombo(m_device_combo, device_names, selected_device, "No devices found");
}

// Populates mono input choices and app-level stereo output-pair choices.
void AudioDeviceSettingsComponent::refreshChannelChoices()
{
    m_output_pairs.clear();
    auto* device = m_staged_device.get();

    m_input_channel_combo.clear(juce::dontSendNotification);
    m_input_channel_combo.setTextWhenNothingSelected("Select...");
    m_input_channel_combo.setTextWhenNoChoicesAvailable("No input channels found");

    m_output_pair_combo.clear(juce::dontSendNotification);
    m_output_pair_combo.setTextWhenNothingSelected("Select...");
    m_output_pair_combo.setTextWhenNoChoicesAvailable("No stereo output pairs found");

    if (device == nullptr)
    {
        return;
    }

    const auto input_names = device->getInputChannelNames();
    for (int index = 0; index < input_names.size(); ++index)
    {
        m_input_channel_combo.addItem(channelName(input_names, index, "Input"), index + 1);
    }

    const int selected_input = m_staged_setup.inputChannels.findNextSetBit(0);
    m_input_channel_combo.setSelectedId(
        juce::isPositiveAndBelow(selected_input, input_names.size()) ? selected_input + 1 : 0,
        juce::dontSendNotification);

    const auto output_names = device->getOutputChannelNames();
    for (int channel_index = 0; channel_index + 1 < output_names.size(); channel_index += 2)
    {
        const OutputPair pair{
            .left_channel = channel_index,
            .right_channel = channel_index + 1,
        };
        m_output_pairs.push_back(pair);
        m_output_pair_combo.addItem(
            outputPairName(output_names, pair.left_channel, pair.right_channel),
            static_cast<int>(m_output_pairs.size()));
    }

    int selected_output_pair_id = m_output_pairs.empty() ? 0 : 1;
    for (int index = 0; index < static_cast<int>(m_output_pairs.size()); ++index)
    {
        const auto pair = m_output_pairs[static_cast<std::size_t>(index)];
        if (m_staged_setup.outputChannels[pair.left_channel] &&
            m_staged_setup.outputChannels[pair.right_channel])
        {
            selected_output_pair_id = index + 1;
            break;
        }
    }

    m_output_pair_combo.setSelectedId(selected_output_pair_id, juce::dontSendNotification);
}

// Populates sample-rate choices from the current open device.
void AudioDeviceSettingsComponent::refreshSampleRateChoices()
{
    m_sample_rates.clear();
    m_sample_rate_combo.clear(juce::dontSendNotification);
    m_sample_rate_combo.setTextWhenNothingSelected("Select...");
    m_sample_rate_combo.setTextWhenNoChoicesAvailable("No sample rates found");

    auto* device = m_staged_device.get();
    if (device == nullptr)
    {
        return;
    }

    if (m_staged_setup.sampleRate <= 0.0 && device->getCurrentSampleRate() > 0.0)
    {
        m_staged_setup.sampleRate = device->getCurrentSampleRate();
    }

    for (const auto sample_rate : device->getAvailableSampleRates())
    {
        m_sample_rates.push_back(sample_rate);
        m_sample_rate_combo.addItem(
            sampleRateText(sample_rate), static_cast<int>(m_sample_rates.size()));
    }

    m_sample_rate_combo.setSelectedId(
        selectedSampleRateId(
            m_sample_rates,
            m_staged_setup.sampleRate > 0.0 ? m_staged_setup.sampleRate
                                            : device->getCurrentSampleRate()),
        juce::dontSendNotification);
}

// Populates buffer-size choices from the current open device.
void AudioDeviceSettingsComponent::refreshBufferSizeChoices()
{
    m_buffer_sizes.clear();
    m_buffer_size_combo.clear(juce::dontSendNotification);
    m_buffer_size_combo.setTextWhenNothingSelected("Select...");
    m_buffer_size_combo.setTextWhenNoChoicesAvailable("No buffer sizes found");

    auto* device = m_staged_device.get();
    if (device == nullptr)
    {
        return;
    }

    if (m_staged_setup.bufferSize <= 0)
    {
        const int current_buffer_size = device->getCurrentBufferSizeSamples();
        m_staged_setup.bufferSize =
            current_buffer_size > 0 ? current_buffer_size : device->getDefaultBufferSize();
    }

    for (const auto buffer_size : device->getAvailableBufferSizes())
    {
        m_buffer_sizes.push_back(buffer_size);
        m_buffer_size_combo.addItem(
            bufferSizeText(buffer_size), static_cast<int>(m_buffer_sizes.size()));
    }

    m_buffer_size_combo.setSelectedId(
        selectedBufferSizeId(
            m_buffer_sizes,
            m_staged_setup.bufferSize > 0 ? m_staged_setup.bufferSize
                                          : device->getCurrentBufferSizeSamples()),
        juce::dontSendNotification);
}

// Enables only controls with available choices or device support.
void AudioDeviceSettingsComponent::refreshControlEnablement()
{
    auto* device = m_device_manager.getCurrentAudioDevice();
    const bool can_use_live_device_buttons = stagedRouteMatchesLiveRoute();
    m_device_type_combo.setEnabled(m_device_type_combo.getNumItems() > 0);
    m_device_combo.setEnabled(m_device_combo.isVisible() && m_device_combo.getNumItems() > 0);
    m_input_device_combo.setEnabled(
        m_input_device_combo.isVisible() && m_input_device_combo.getNumItems() > 0);
    m_output_device_combo.setEnabled(
        m_output_device_combo.isVisible() && m_output_device_combo.getNumItems() > 0);
    m_input_channel_combo.setEnabled(m_input_channel_combo.getNumItems() > 0);
    m_output_pair_combo.setEnabled(m_output_pair_combo.getNumItems() > 0);
    m_sample_rate_combo.setEnabled(m_sample_rate_combo.getNumItems() > 0);
    m_buffer_size_combo.setEnabled(m_buffer_size_combo.getNumItems() > 0);
    m_test_button.setEnabled(can_use_live_device_buttons && device != nullptr);
    m_control_panel_button.setEnabled(
        can_use_live_device_buttons && device != nullptr && device->hasControlPanel());
}

// Switches the current JUCE device type and lets the manager pick that type's current route.
void AudioDeviceSettingsComponent::handleDeviceTypeChanged()
{
    if (m_device_type_combo.getSelectedItemIndex() < 0)
    {
        return;
    }

    const juce::String type_name = m_device_type_combo.getText();
    if (type_name.isEmpty() || type_name == m_staged_device_type)
    {
        return;
    }

    m_error_label.setText(juce::String{}, juce::dontSendNotification);
    m_staged_device_type = type_name;
    m_staged_setup.inputDeviceName.clear();
    m_staged_setup.outputDeviceName.clear();
    m_staged_setup.sampleRate = 0.0;
    m_staged_setup.bufferSize = 0;
    resetStagedRouteDefaults();
    refreshControls();
}

// Opens the selected input/output device using Rock Hero's first mono/stereo route.
void AudioDeviceSettingsComponent::handleDeviceChanged()
{
    applySelectedDevice();
}

// Applies the selected mono input channel and stereo output pair.
void AudioDeviceSettingsComponent::handleRouteChanged()
{
    applySelectedRoute();
}

// Applies sample-rate and buffer-size changes while preserving the selected route.
void AudioDeviceSettingsComponent::handleAudioFormatChanged()
{
    applySelectedRoute();
}

// Applies the staged settings and closes the dialog on success.
void AudioDeviceSettingsComponent::acceptAndClose()
{
    applyAcceptedSetup();
}

// Restores the captured state and closes the dialog.
void AudioDeviceSettingsComponent::cancelAndClose()
{
    closeDialog();
}

// Stages a newly selected device and resets channel choices to the app-supported shape.
void AudioDeviceSettingsComponent::applySelectedDevice()
{
    if (!copySelectedDeviceNames(m_staged_setup))
    {
        return;
    }

    m_error_label.setText(juce::String{}, juce::dontSendNotification);
    m_staged_setup.sampleRate = 0.0;
    m_staged_setup.bufferSize = 0;
    resetStagedRouteDefaults();
    refreshControls();
}

// Stages current route, sample-rate, and buffer-size selections.
void AudioDeviceSettingsComponent::applySelectedRoute()
{
    if (!copySelectedDeviceNames(m_staged_setup))
    {
        return;
    }

    const int input_channel_index = m_input_channel_combo.getSelectedItemIndex();
    if (input_channel_index >= 0)
    {
        setSingleInputChannel(m_staged_setup, input_channel_index);
    }

    const int output_pair_index = m_output_pair_combo.getSelectedItemIndex();
    if (juce::isPositiveAndBelow(output_pair_index, static_cast<int>(m_output_pairs.size())))
    {
        setOutputPair(m_staged_setup, m_output_pairs[static_cast<std::size_t>(output_pair_index)]);
    }

    setSelectedAudioFormat(m_staged_setup);
    m_error_label.setText(juce::String{}, juce::dontSendNotification);
    refreshControlEnablement();
}

// Applies the staged route to the live device manager only when the user accepts the dialog.
void AudioDeviceSettingsComponent::applyAcceptedSetup()
{
    const auto previous_setup = m_device_manager.getAudioDeviceSetup();
    const juce::String previous_device_type = m_device_manager.getCurrentAudioDeviceType();

    if (m_staged_device_type.isNotEmpty() &&
        m_staged_device_type != m_device_manager.getCurrentAudioDeviceType())
    {
        m_device_manager.setCurrentAudioDeviceType(m_staged_device_type, true);
    }

    const juce::String error = m_device_manager.setAudioDeviceSetup(m_staged_setup, true);
    if (error.isNotEmpty())
    {
        if (previous_device_type.isNotEmpty() &&
            previous_device_type != m_device_manager.getCurrentAudioDeviceType())
        {
            m_device_manager.setCurrentAudioDeviceType(previous_device_type, true);
        }

        auto setup = previous_setup;
        m_device_manager.setAudioDeviceSetup(setup, true);
        m_error_label.setText(error, juce::dontSendNotification);
        refreshControls();
        return;
    }

    closeDialog();
}

// Requests modal shutdown from the host DialogWindow.
void AudioDeviceSettingsComponent::closeDialog()
{
    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
    {
        dialog->exitModalState(0);
    }
}

// Chooses a staged audio system from the live device manager's available types.
void AudioDeviceSettingsComponent::ensureStagedDeviceType()
{
    const auto& device_types = m_device_manager.getAvailableDeviceTypes();

    for (const auto* type : device_types)
    {
        if (type->getTypeName() == m_staged_device_type)
        {
            return;
        }
    }

    const juce::String live_device_type = m_device_manager.getCurrentAudioDeviceType();
    for (const auto* type : device_types)
    {
        if (type->getTypeName() == live_device_type)
        {
            m_staged_device_type = live_device_type;
            return;
        }
    }

    m_staged_device_type =
        device_types.isEmpty() ? juce::String{} : device_types.getUnchecked(0)->getTypeName();
}

// Keeps staged device names valid for the selected audio system without opening the live route.
void AudioDeviceSettingsComponent::ensureStagedDeviceNames()
{
    auto* type = currentDeviceType();
    if (type == nullptr)
    {
        m_staged_setup.inputDeviceName.clear();
        m_staged_setup.outputDeviceName.clear();
        return;
    }

    type->scanForDevices();

    if (type->hasSeparateInputsAndOutputs())
    {
        m_staged_setup.inputDeviceName = validOrDefaultDeviceName(
            m_staged_setup.inputDeviceName,
            type->getDeviceNames(true),
            type->getDefaultDeviceIndex(true));
        m_staged_setup.outputDeviceName = validOrDefaultDeviceName(
            m_staged_setup.outputDeviceName,
            type->getDeviceNames(false),
            type->getDefaultDeviceIndex(false));
        return;
    }

    auto device_names = type->getDeviceNames(false);
    if (device_names.isEmpty())
    {
        device_names = type->getDeviceNames(true);
    }

    const juce::String requested_name = m_staged_setup.outputDeviceName.isNotEmpty()
                                            ? m_staged_setup.outputDeviceName
                                            : m_staged_setup.inputDeviceName;
    const juce::String device_name =
        validOrDefaultDeviceName(requested_name, device_names, type->getDefaultDeviceIndex(false));
    m_staged_setup.inputDeviceName = device_name;
    m_staged_setup.outputDeviceName = device_name;
}

// Sets the staged route to one mono input and one stereo output pair.
void AudioDeviceSettingsComponent::resetStagedRouteDefaults()
{
    setSingleInputChannel(m_staged_setup, 0);
    setOutputPair(m_staged_setup, OutputPair{.left_channel = 0, .right_channel = 1});
}

// Returns the currently selected JUCE device type object, if one exists.
juce::AudioIODeviceType* AudioDeviceSettingsComponent::currentDeviceType() const
{
    const auto& device_types = m_device_manager.getAvailableDeviceTypes();
    for (auto* type : device_types)
    {
        if (type->getTypeName() == m_staged_device_type)
        {
            return type;
        }
    }

    return nullptr;
}

// Reports whether the current backend type exposes separate input and output devices.
bool AudioDeviceSettingsComponent::currentTypeUsesSeparateDevices() const
{
    auto* type = currentDeviceType();
    return type != nullptr && type->hasSeparateInputsAndOutputs();
}

// Creates a non-open staged device used only to inspect channel and format capabilities.
std::unique_ptr<juce::AudioIODevice> AudioDeviceSettingsComponent::createStagedDevice() const
{
    auto* type = currentDeviceType();
    if (type == nullptr || m_staged_setup.inputDeviceName.isEmpty() ||
        m_staged_setup.outputDeviceName.isEmpty())
    {
        return nullptr;
    }

    type->scanForDevices();
    return std::unique_ptr<juce::AudioIODevice>{type->createDevice(
        m_staged_setup.outputDeviceName, m_staged_setup.inputDeviceName)};
}

// Reports whether live-device-only buttons apply to the currently staged route.
bool AudioDeviceSettingsComponent::stagedRouteMatchesLiveRoute() const
{
    return m_staged_device_type == m_device_manager.getCurrentAudioDeviceType() &&
           m_staged_setup == m_device_manager.getAudioDeviceSetup();
}

// Copies the currently selected device names into a setup.
bool AudioDeviceSettingsComponent::copySelectedDeviceNames(
    juce::AudioDeviceManager::AudioDeviceSetup& setup) const
{
    if (currentTypeUsesSeparateDevices())
    {
        if (m_input_device_combo.getSelectedItemIndex() < 0 ||
            m_output_device_combo.getSelectedItemIndex() < 0)
        {
            return false;
        }

        const juce::String input_name = m_input_device_combo.getText();
        const juce::String output_name = m_output_device_combo.getText();
        if (input_name.isEmpty() || output_name.isEmpty())
        {
            return false;
        }

        setup.inputDeviceName = input_name;
        setup.outputDeviceName = output_name;
        return true;
    }

    if (m_device_combo.getSelectedItemIndex() < 0)
    {
        return false;
    }

    const juce::String device_name = m_device_combo.getText();
    if (device_name.isEmpty())
    {
        return false;
    }

    setup.inputDeviceName = device_name;
    setup.outputDeviceName = device_name;
    return true;
}

// Replaces the active input channels with exactly one mono input channel.
void AudioDeviceSettingsComponent::setSingleInputChannel(
    juce::AudioDeviceManager::AudioDeviceSetup& setup, int channel_index) const
{
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (channel_index >= 0)
    {
        setup.inputChannels.setBit(channel_index);
    }
}

// Replaces the active output channels with exactly one stereo output pair.
void AudioDeviceSettingsComponent::setOutputPair(
    juce::AudioDeviceManager::AudioDeviceSetup& setup, OutputPair pair) const
{
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setBit(pair.left_channel);
    setup.outputChannels.setBit(pair.right_channel);
}

// Copies selected sample-rate and buffer-size choices into the setup when available.
void AudioDeviceSettingsComponent::setSelectedAudioFormat(
    juce::AudioDeviceManager::AudioDeviceSetup& setup) const
{
    const int sample_rate_index = m_sample_rate_combo.getSelectedItemIndex();
    if (juce::isPositiveAndBelow(sample_rate_index, static_cast<int>(m_sample_rates.size())))
    {
        setup.sampleRate = m_sample_rates[static_cast<std::size_t>(sample_rate_index)];
    }

    const int buffer_size_index = m_buffer_size_combo.getSelectedItemIndex();
    if (juce::isPositiveAndBelow(buffer_size_index, static_cast<int>(m_buffer_sizes.size())))
    {
        setup.bufferSize = m_buffer_sizes[static_cast<std::size_t>(buffer_size_index)];
    }
}

} // namespace rock_hero::editor::ui
