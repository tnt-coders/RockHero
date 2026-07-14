#include "audio_device_settings_view.h"

#include <algorithm>
#include <utility>

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
constexpr int g_max_window_width{1000};
constexpr int g_max_window_height{760};
constexpr int g_toggle_row_height{26};

// Hover text shown on the read-only device fields while the "use game audio settings" toggle is on,
// explaining why they cannot be edited. Cleared when the editor owns its own audio route.
constexpr const char* g_game_settings_tooltip{"Derived from game settings"};

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
[[nodiscard]] int windowHeightForRows(int row_count) noexcept
{
    const int controls_height = formRowsHeight(row_count);
    return (g_content_inset * 2) + controls_height + g_row_gap + g_error_height + g_row_gap +
           g_row_height;
}

// Uses stable one-based ComboBox item IDs supplied by the controller state.
void populateChoiceCombo(
    juce::ComboBox& combo, const std::vector<core::AudioDeviceSettingsViewState::Choice>& choices,
    int selected_id, const juce::String& empty_text)
{
    combo.clear(juce::dontSendNotification);
    combo.setTextWhenNothingSelected("Select...");
    combo.setTextWhenNoChoicesAvailable(empty_text);

    for (const auto& choice : choices)
    {
        combo.addItem(juce::String{choice.label.c_str()}, choice.id);
    }

    combo.setSelectedId(selected_id, juce::dontSendNotification);
}

// Lays out one label/control row when that row is visible for the current audio system.
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

// Creates the passive settings view and wires user gestures to the controller contract.
AudioDeviceSettingsView::AudioDeviceSettingsView(
    core::IAudioDeviceSettingsController& controller, ApplyingCallback applying_callback,
    CloseCallback close_callback)
    : m_controller(controller)
    , m_applying_callback(std::move(applying_callback))
    , m_close_callback(std::move(close_callback))
{
    configureControls();
    applyStateToControls();
}

AudioDeviceSettingsView::~AudioDeviceSettingsView() = default;

// Returns a default width wide enough for typical audio-system, device, and channel names.
int AudioDeviceSettingsView::preferredWidth() noexcept
{
    return g_preferred_width;
}

// Returns a derived height from the rows visible for the currently selected audio system, plus the
// "use game audio settings" toggle row. The read-only game reflection reuses the same device rows
// (locked, with an explanatory tooltip) so it needs no extra vertical allowance.
int AudioDeviceSettingsView::preferredContentHeight() const noexcept
{
    const int visible_rows =
        g_min_control_rows + (m_state.uses_separate_input_output_devices ? 1 : 0);
    return windowHeightForRows(visible_rows) + g_toggle_row_height + g_row_gap;
}

// Keeps the route selectors usable without requiring the initial window to be very wide.
int AudioDeviceSettingsView::minimumWidth() noexcept
{
    return 520;
}

// Caps window resizing at a useful settings-window width.
int AudioDeviceSettingsView::maximumWidth() noexcept
{
    return g_max_window_width;
}

// Caps window resizing at a useful settings-window height.
int AudioDeviceSettingsView::maximumHeight() noexcept
{
    return g_max_window_height;
}

// Applies controller-derived choices, selections, visibility, enablement, and error text.
void AudioDeviceSettingsView::setState(const core::AudioDeviceSettingsViewState& state)
{
    m_state = state;
    applyStateToControls();
    syncWindowHeightToContent();
    resized();
}

// Stores the host callback fired when the user changes the "use game audio settings" toggle.
void AudioDeviceSettingsView::setGameAudioSettingsChangedCallback(
    GameAudioSettingsChangedCallback callback)
{
    m_on_use_game_settings_changed = std::move(callback);
}

// Applies the resolved toggle/availability state and re-scopes the panel between the read-only
// game reflection and the editable editor-own device flow.
void AudioDeviceSettingsView::setGameAudioSettings(GameAudioSettingsState state)
{
    m_game_settings = state;
    applyGameAudioSettingsPresentation();
    applyStateToControls();
    syncWindowHeightToContent();
    resized();
}

// The toggle being on renders the device fields read-only whether or not a game config exists: an
// unconfigured game still locks the fields and points the user at the opt-out rather than letting
// them edit a route the toggle claims the game owns.
bool AudioDeviceSettingsView::gameSettingsLockActive() const noexcept
{
    return m_game_settings.use_game_settings;
}

// Reflects the toggle value; the read-only field enablement and the derived-from-game tooltip are
// applied alongside the other control state in applyStateToControls().
void AudioDeviceSettingsView::applyGameAudioSettingsPresentation()
{
    m_use_game_settings_toggle.setToggleState(
        m_game_settings.use_game_settings, juce::dontSendNotification);
}

// Requests modal shutdown from the host DialogWindow.
void AudioDeviceSettingsView::requestClose()
{
    if (m_close_callback)
    {
        m_close_callback();
        return;
    }

    closeWindow();
}

// Disables editing while audio-device open work runs and delegates host visibility to the
// window wrapper that owns JUCE modal lifetime.
void AudioDeviceSettingsView::setApplying(bool applying)
{
    if (m_applying == applying)
    {
        return;
    }

    m_applying = applying;
    setMouseCursor(applying ? juce::MouseCursor::WaitCursor : juce::MouseCursor::NormalCursor);
    applyStateToControls();
    if (m_applying_callback)
    {
        m_applying_callback(applying);
    }
    repaint();
}

// Positions route rows, status text, utility buttons, and final window actions.
void AudioDeviceSettingsView::resized()
{
    auto area = getLocalBounds().reduced(g_content_inset);
    auto button_row = area.removeFromBottom(g_row_height);

    m_cancel_button.setBounds(button_row.removeFromRight(g_button_width));
    button_row.removeFromRight(std::min(g_row_gap, button_row.getWidth()));
    m_ok_button.setBounds(button_row.removeFromRight(g_button_width));

    if (m_control_panel_button.isVisible())
    {
        m_control_panel_button.setBounds(
            button_row.removeFromLeft(std::min(g_utility_button_width, button_row.getWidth())));
    }
    else
    {
        m_control_panel_button.setBounds({});
    }

    area.removeFromBottom(std::min(g_row_gap, area.getHeight()));
    m_error_label.setBounds(area.removeFromBottom(std::min(g_error_height, area.getHeight())));
    area.removeFromBottom(std::min(g_row_gap, area.getHeight()));

    // The toggle sits above the device rows so the read-only effect of the toggle on the fields
    // below is visually obvious (open question 3: top-of-panel placement).
    m_use_game_settings_toggle.setBounds(area.removeFromTop(g_toggle_row_height));
    area.removeFromTop(std::min(g_row_gap, area.getHeight()));

    layoutRow(m_device_type_label, m_device_type_combo, area);
    layoutRow(m_device_label, m_device_combo, area);
    layoutRow(m_output_device_label, m_output_device_combo, area);
    layoutRow(m_input_device_label, m_input_device_combo, area);
    layoutRow(m_output_pair_label, m_output_pair_combo, area);
    layoutRow(m_input_channel_label, m_input_channel_combo, area);
    layoutRow(m_sample_rate_label, m_sample_rate_combo, area);
    layoutRow(m_buffer_size_label, m_buffer_size_combo, area);
}

// Configures labels, callbacks, and static button presentation.
void AudioDeviceSettingsView::configureControls()
{
    setComponentID("audio_device_settings_view");

    m_use_game_settings_toggle.setComponentID("audio_settings_use_game_toggle");
    m_use_game_settings_toggle.setButtonText("Use game audio settings");
    m_use_game_settings_toggle.onClick = [this] {
        // Update the local read-only presentation immediately so the panel reflects the flip without
        // waiting for the controller round-trip, then notify the host to drive the source switch.
        m_game_settings.use_game_settings = m_use_game_settings_toggle.getToggleState();
        applyGameAudioSettingsPresentation();
        applyStateToControls();
        syncWindowHeightToContent();
        resized();
        if (m_on_use_game_settings_changed)
        {
            m_on_use_game_settings_changed(m_game_settings.use_game_settings);
        }
    };

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
    m_control_panel_button.setComponentID("audio_settings_control_panel_button");
    m_ok_button.setComponentID("audio_settings_ok_button");
    m_cancel_button.setComponentID("audio_settings_cancel_button");

    m_error_label.setColour(juce::Label::textColourId, juce::Colours::lightsalmon);
    m_error_label.setJustificationType(juce::Justification::centredLeft);
    m_control_panel_button.setButtonText("Control Panel");
    m_ok_button.setButtonText("OK");
    m_cancel_button.setButtonText("Cancel");

    // populateChoiceCombo() uses juce::dontSendNotification on clear() and setSelectedId(), so
    // refresh-driven updates never invoke these callbacks. They fire only on user gestures.
    m_device_type_combo.onChange = [this] {
        m_controller.onAudioSystemSelected(m_device_type_combo.getSelectedId());
    };
    m_device_combo.onChange = [this] {
        m_controller.onDeviceSelected(m_device_combo.getSelectedId());
    };
    m_input_device_combo.onChange = [this] {
        m_controller.onInputDeviceSelected(m_input_device_combo.getSelectedId());
    };
    m_output_device_combo.onChange = [this] {
        m_controller.onOutputDeviceSelected(m_output_device_combo.getSelectedId());
    };
    m_input_channel_combo.onChange = [this] {
        m_controller.onInputChannelSelected(m_input_channel_combo.getSelectedId());
    };
    m_output_pair_combo.onChange = [this] {
        m_controller.onStereoOutputPairSelected(m_output_pair_combo.getSelectedId());
    };
    m_sample_rate_combo.onChange = [this] {
        m_controller.onSampleRateSelected(m_sample_rate_combo.getSelectedId());
    };
    m_buffer_size_combo.onChange = [this] {
        m_controller.onBufferSizeSelected(m_buffer_size_combo.getSelectedId());
    };
    m_control_panel_button.onClick = [this] { m_controller.onControlPanelRequested(); };
    m_ok_button.onClick = [this] {
        // With the game source active the fields are read-only and there is nothing to apply, so OK
        // just closes the window, exactly like Cancel; only the editable editor-own flow runs a real
        // device apply.
        if (gameSettingsLockActive())
        {
            m_controller.onCancelRequested();
        }
        else
        {
            m_controller.onOkRequested();
        }
    };
    m_cancel_button.onClick = [this] { m_controller.onCancelRequested(); };

    addAndMakeVisible(m_use_game_settings_toggle);
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
    addAndMakeVisible(m_control_panel_button);
    addAndMakeVisible(m_ok_button);
    addAndMakeVisible(m_cancel_button);
}

// Rebuilds controls from the last controller-derived state.
void AudioDeviceSettingsView::applyStateToControls()
{
    populateChoiceCombo(
        m_device_type_combo,
        m_state.audio_systems,
        m_state.selected_audio_system_id,
        "No audio systems found");
    populateChoiceCombo(
        m_device_combo, m_state.devices, m_state.selected_device_id, "No devices found");
    populateChoiceCombo(
        m_input_device_combo,
        m_state.input_devices,
        m_state.selected_input_device_id,
        "No input devices found");
    populateChoiceCombo(
        m_output_device_combo,
        m_state.output_devices,
        m_state.selected_output_device_id,
        "No output devices found");
    populateChoiceCombo(
        m_input_channel_combo,
        m_state.input_channels,
        m_state.selected_input_channel_id,
        "No input channels found");
    populateChoiceCombo(
        m_output_pair_combo,
        m_state.stereo_output_pairs,
        m_state.selected_stereo_output_pair_id,
        "No stereo output pairs found");
    populateChoiceCombo(
        m_sample_rate_combo,
        m_state.sample_rates,
        m_state.selected_sample_rate_id,
        "No sample rates found");
    populateChoiceCombo(
        m_buffer_size_combo,
        m_state.buffer_sizes,
        m_state.selected_buffer_size_id,
        "No buffer sizes found");

    const bool separate_devices = m_state.uses_separate_input_output_devices;
    m_device_label.setVisible(!separate_devices);
    m_device_combo.setVisible(!separate_devices);
    m_input_device_label.setVisible(separate_devices);
    m_input_device_combo.setVisible(separate_devices);
    m_output_device_label.setVisible(separate_devices);
    m_output_device_combo.setVisible(separate_devices);

    // The device fields stay editable only while the editor owns its own audio route. With the
    // toggle on they are read-only reflections of the game's configuration (or, when the game is
    // unconfigured, locked to steer the user to the opt-out); an in-flight apply also disables them.
    const bool controls_enabled = !m_applying && !gameSettingsLockActive();
    m_device_type_combo.setEnabled(controls_enabled && !m_state.audio_systems.empty());
    m_device_combo.setEnabled(
        controls_enabled && m_device_combo.isVisible() && !m_state.devices.empty());
    m_input_device_combo.setEnabled(
        controls_enabled && m_input_device_combo.isVisible() && !m_state.input_devices.empty());
    m_output_device_combo.setEnabled(
        controls_enabled && m_output_device_combo.isVisible() && !m_state.output_devices.empty());
    m_input_channel_combo.setEnabled(controls_enabled && !m_state.input_channels.empty());
    m_output_pair_combo.setEnabled(controls_enabled && !m_state.stereo_output_pairs.empty());
    m_sample_rate_combo.setEnabled(controls_enabled && !m_state.sample_rates.empty());
    m_buffer_size_combo.setEnabled(controls_enabled && !m_state.buffer_sizes.empty());
    // The control panel button is hidden entirely for backends that do not expose one (such as
    // WASAPI). Only ASIO drivers reliably show a per-device control panel; for non-ASIO routes
    // the button would otherwise sit there as a permanently-disabled control and look broken.
    m_control_panel_button.setVisible(m_state.control_panel_enabled);
    m_control_panel_button.setEnabled(controls_enabled && m_state.control_panel_enabled);
    // OK stays enabled while the game source is active even though the fields are locked: there it
    // just closes the window like Cancel (there is nothing to apply), so it must not look dead.
    m_ok_button.setEnabled(!m_applying && (m_state.ok_enabled || gameSettingsLockActive()));
    // Cancel closes the window in either source mode, so it follows only the apply fence, not the
    // read-only game lock. The toggle stays usable while locked so the user can always uncheck it to
    // switch back to the editor's own audio.
    m_cancel_button.setEnabled(!m_applying);
    m_use_game_settings_toggle.setEnabled(!m_applying);

    // While the game source is active the locked device fields carry a hover tooltip explaining why
    // they cannot be edited; the tooltip is cleared when the editor owns its own audio route.
    const juce::String field_tooltip{gameSettingsLockActive() ? g_game_settings_tooltip : ""};
    m_device_type_combo.setTooltip(field_tooltip);
    m_device_combo.setTooltip(field_tooltip);
    m_input_device_combo.setTooltip(field_tooltip);
    m_output_device_combo.setTooltip(field_tooltip);
    m_input_channel_combo.setTooltip(field_tooltip);
    m_output_pair_combo.setTooltip(field_tooltip);
    m_sample_rate_combo.setTooltip(field_tooltip);
    m_buffer_size_combo.setTooltip(field_tooltip);

    m_error_label.setText(juce::String{m_state.error_message.c_str()}, juce::dontSendNotification);
}

// Keeps the host window's resize limits matched to the current content. JUCE auto-grows the
// DialogWindow when the new minimum height exceeds its current size, so adding rows (for example
// switching from a combined-device backend to separate input/output) widens the window. Limits
// alone, without an unconditional setSize, also leave user-expanded windows intact rather than
// shrinking them back to preferred size when the row count drops.
void AudioDeviceSettingsView::syncWindowHeightToContent()
{
    const int content_height = preferredContentHeight();

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
    {
        window->setResizeLimits(minimumWidth(), content_height, maximumWidth(), maximumHeight());
        return;
    }

    // No host window yet (initial setState during controller attachView before the view is added
    // to the window content). Size the view itself so layout calculations produce sensible
    // bounds before the dialog hosts us.
    setSize(getWidth() > 0 ? getWidth() : preferredWidth(), content_height);
}

// Requests modal shutdown from the host DialogWindow.
void AudioDeviceSettingsView::closeWindow()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
    {
        window->exitModalState(0);
    }
}

} // namespace rock_hero::editor::ui
