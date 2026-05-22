/*!
\file audio_device_settings_component.h
\brief Private editor UI component for Rock Hero audio hardware routing.
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Presents the app-specific audio-device settings supported by Rock Hero.

The component stages hardware route changes locally while the dialog is open. Pressing OK applies
the selected route through juce::AudioDeviceManager; Cancel, Escape, and native close leave the
active device untouched.
*/
class AudioDeviceSettingsComponent final : public juce::Component, private juce::ChangeListener
{
public:
    /*!
    \brief Creates the audio settings component around the shared device manager.
    \param device_manager Device manager owned by the audio backend.
    */
    explicit AudioDeviceSettingsComponent(juce::AudioDeviceManager& device_manager);

    /*! \brief Removes device-change listeners without mutating the active audio route. */
    ~AudioDeviceSettingsComponent() override;

    /*! \brief Returns the default dialog width for the current control set. */
    [[nodiscard]] static int preferredWidth() noexcept;

    /*! \brief Returns the preferred dialog height for the currently visible controls. */
    [[nodiscard]] int preferredContentHeight() const noexcept;

    /*! \brief Returns the minimum usable dialog width. */
    [[nodiscard]] static int minimumWidth() noexcept;

    /*! \brief Returns the maximum useful dialog width. */
    [[nodiscard]] static int maximumWidth() noexcept;

    /*! \brief Returns the maximum useful dialog height. */
    [[nodiscard]] static int maximumHeight() noexcept;

    /*! \brief Lays out the routing controls and dialog action buttons. */
    void resized() override;

private:
    struct OutputPair
    {
        int left_channel{};
        int right_channel{};
    };

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Sets labels, button text, component IDs, and static presentation properties.
    void configureControls();

    // Rebuilds all controls from the active device-manager state.
    void refreshControls();

    // Rebuilds driver type choices from the device manager.
    void refreshDeviceTypes();

    // Scans the currently staged device type once for this refresh pass.
    void scanCurrentDeviceType() const;

    // Rebuilds device-name choices for the current driver type.
    void refreshDeviceNames();

    // Rebuilds the mono input and stereo output-pair choices from the open device.
    void refreshChannelChoices();

    // Rebuilds sample-rate choices from the staged device capabilities.
    void refreshSampleRateChoices();

    // Rebuilds buffer-size choices from the open device.
    void refreshBufferSizeChoices();

    // Enables controls only when their backing choices exist.
    void refreshControlEnablement();

    // Resizes the component and host dialog to match the current form rows.
    void syncDialogHeightToContent();

    void handleDeviceTypeChanged();
    void handleDeviceChanged();
    void handleRouteChanged();
    void handleAudioFormatChanged();
    void acceptAndClose();
    void cancelAndClose();

    // Stages the selected device names and resets routing to the first usable channels.
    void applySelectedDevice();

    // Stages channel, sample-rate, and buffer-size selections against the chosen devices.
    void applySelectedRoute();

    // Applies the staged route to the active device manager and reports any failure.
    void applyAcceptedSetup();

    // Closes the containing DialogWindow, if the component is currently hosted by one.
    void closeDialog();

    // Ensures a staged audio system exists before dependent controls are populated.
    void ensureStagedDeviceType();

    // Ensures staged device names refer to available devices for the chosen audio system.
    void ensureStagedDeviceNames();

    // Resets the staged route to Rock Hero's first mono input and stereo output pair.
    void resetStagedRouteDefaults();

    [[nodiscard]] juce::AudioIODeviceType* currentDeviceType() const;
    [[nodiscard]] bool currentTypeUsesSeparateDevices() const;
    [[nodiscard]] std::unique_ptr<juce::AudioIODevice> createStagedDevice() const;
    [[nodiscard]] bool stagedRouteMatchesActiveRoute() const;
    [[nodiscard]] bool stagedDeviceNamesMatchActiveRoute() const;
    [[nodiscard]] bool copySelectedDeviceNames(
        juce::AudioDeviceManager::AudioDeviceSetup& setup) const;

    void setSingleInputChannel(
        juce::AudioDeviceManager::AudioDeviceSetup& setup, int channel_index) const;
    void setOutputPair(juce::AudioDeviceManager::AudioDeviceSetup& setup, OutputPair pair) const;
    void setSelectedAudioFormat(juce::AudioDeviceManager::AudioDeviceSetup& setup) const;

    juce::AudioDeviceManager& m_device_manager;
    juce::AudioDeviceManager::AudioDeviceSetup m_staged_setup;
    juce::String m_staged_device_type;
    std::unique_ptr<juce::AudioIODevice> m_staged_device;
    std::vector<double> m_sample_rates;
    std::vector<int> m_buffer_sizes;
    std::vector<OutputPair> m_output_pairs;

    juce::Label m_device_type_label;
    juce::ComboBox m_device_type_combo;
    juce::Label m_device_label;
    juce::ComboBox m_device_combo;
    juce::Label m_input_device_label;
    juce::ComboBox m_input_device_combo;
    juce::Label m_output_device_label;
    juce::ComboBox m_output_device_combo;
    juce::Label m_input_channel_label;
    juce::ComboBox m_input_channel_combo;
    juce::Label m_output_pair_label;
    juce::ComboBox m_output_pair_combo;
    juce::Label m_sample_rate_label;
    juce::ComboBox m_sample_rate_combo;
    juce::Label m_buffer_size_label;
    juce::ComboBox m_buffer_size_combo;
    juce::Label m_error_label;
    juce::TextButton m_test_button;
    juce::TextButton m_control_panel_button;
    juce::TextButton m_ok_button;
    juce::TextButton m_cancel_button;

    bool m_refreshing_controls{false};
};

} // namespace rock_hero::editor::ui
