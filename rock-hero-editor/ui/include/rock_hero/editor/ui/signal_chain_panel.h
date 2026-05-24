/*!
\file signal_chain_panel.h
\brief JUCE control panel for the plugin chain on the instrument route.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>
#include <rock_hero/editor/ui/audio_level_meter.h>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Displays the current plugin chain and emits panel intents.

The panel is intentionally linear for the first plugin-host UI. It renders project-owned state and
emits add-plugin intent through Listener so future rack or parallel-chain models can replace the
state shape without exposing Tracktion or JUCE plugin descriptions to the view.
*/
class SignalChainPanel final : public juce::Component
{
public:
    /*! \brief Listener for user intents emitted by the signal-chain panel. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*! \brief Called when the user requests the plugin browser for the chain. */
        virtual void onAddPluginPressed() = 0;

        /*!
        \brief Called when the user requests removal of a plugin instance.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onRemovePluginPressed(std::string instance_id) = 0;

        /*!
        \brief Called when the user requests a plugin instance editor window.
        \param instance_id Opaque plugin instance ID selected by the user.
        */
        virtual void onOpenPluginPressed(std::string instance_id) = 0;

        /*!
        \brief Called when the user adjusts the input gain slider.
        \param gain_db New input gain in decibels.
        */
        virtual void onInputGainChanged(double gain_db) = 0;

        /*!
        \brief Called when the user adjusts the output gain slider.
        \param gain_db New output gain in decibels.
        */
        virtual void onOutputGainChanged(double gain_db) = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates the panel and stores the listener that receives user intent.
    \param listener Listener that receives panel actions.
    */
    explicit SignalChainPanel(Listener& listener);

    /*! \brief Releases child controls. */
    ~SignalChainPanel() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    SignalChainPanel(const SignalChainPanel&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    SignalChainPanel& operator=(const SignalChainPanel&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    SignalChainPanel(SignalChainPanel&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    SignalChainPanel& operator=(SignalChainPanel&&) = delete;

    /*!
    \brief Applies the current signal-chain render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::SignalChainViewState& state);

    /*!
    \brief Applies live-rig post-fader meter levels.
    \param input_level Level after the input gain fader.
    \param output_level Level after the output gain fader.
    */
    void setMeterLevels(
        common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level);

    /*!
    \brief Paints the panel background, title, and plugin chain.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the add button within the panel. */
    void resized() override;

private:
    class PluginRowView;

    // Rebuilds row components after controller-derived plugin state changes.
    void rebuildPluginRows();

    // Listener that receives panel user intents.
    Listener& m_listener;

    // Last render state pushed by the editor controller.
    core::SignalChainViewState m_state{};

    // Button that opens plugin selection through the owning editor view.
    juce::TextButton m_add_plugin_button;

    // Input gain slider positioned on the left side of the plugin chain.
    juce::Slider m_input_gain_slider;

    // Post-input-gain peak meter positioned beside the input slider.
    AudioLevelMeter m_input_meter;

    // Output gain slider positioned on the right side of the plugin chain.
    juce::Slider m_output_gain_slider;

    // Post-output-gain peak meter positioned beside the output slider.
    AudioLevelMeter m_output_meter;

    // Child row controls for the current plugin chain.
    std::vector<std::unique_ptr<PluginRowView>> m_plugin_rows;
};

} // namespace rock_hero::editor::ui
