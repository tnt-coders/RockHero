/*!
\file instrument_panel.h
\brief JUCE control panel for the plugin chain on the instrument route.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/instrument_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Displays the current plugin chain and emits panel intents.

The panel is intentionally linear for the first plugin-host UI. It renders project-owned state and
emits add-plugin intent through Listener so future rack or parallel-chain models can replace the
state shape without exposing Tracktion or JUCE plugin descriptions to the view.
*/
class InstrumentPanel final : public juce::Component
{
public:
    /*! \brief Listener for user intents emitted by the instrument panel. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*! \brief Called when the user requests a plugin file for the chain. */
        virtual void onAddPluginPressed() = 0;

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
    explicit InstrumentPanel(Listener& listener);

    /*! \brief Releases child controls. */
    ~InstrumentPanel() override;

    /*! \brief Copying is disabled because JUCE component ownership is not copyable. */
    InstrumentPanel(const InstrumentPanel&) = delete;

    /*! \brief Copy assignment is disabled because JUCE component ownership is not copyable. */
    InstrumentPanel& operator=(const InstrumentPanel&) = delete;

    /*! \brief Moving is disabled because child component registrations are not movable. */
    InstrumentPanel(InstrumentPanel&&) = delete;

    /*! \brief Move assignment is disabled because child registrations are not movable. */
    InstrumentPanel& operator=(InstrumentPanel&&) = delete;

    /*!
    \brief Applies the current instrument render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::InstrumentViewState& state);

    /*!
    \brief Paints the panel background, title, and plugin chain.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Lays out the add button within the panel. */
    void resized() override;

private:
    // Listener that receives panel user intents.
    Listener& m_listener;

    // Last render state pushed by the editor controller.
    core::InstrumentViewState m_state{};

    // Button that opens plugin selection through the owning editor view.
    juce::TextButton m_add_plugin_button;
};

} // namespace rock_hero::editor::ui
