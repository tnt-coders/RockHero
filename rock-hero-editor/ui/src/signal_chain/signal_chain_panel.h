/*!
\file signal_chain_panel.h
\brief JUCE panel that hosts the signal-chain view in the editor layout.
*/

#pragma once

#include "signal_chain/signal_chain_view.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/audio/audio_meter_snapshot.h>
#include <rock_hero/editor/core/signal_chain_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Hosts SignalChainView in the editor's bottom control-panel region.

The panel preserves the editor layout concept from the architecture docs while the child view owns
the concrete SignalChainViewState rendering and signal-chain interaction wiring.
*/
class SignalChainPanel final : public juce::Component
{
public:
    /*! \brief Listener for user intents emitted by the hosted signal-chain view. */
    using Listener = SignalChainView::Listener;

    /*!
    \brief Creates the panel and its hosted signal-chain view.
    \param listener Listener that receives signal-chain actions.
    */
    explicit SignalChainPanel(Listener& listener);

    /*! \brief Releases the hosted signal-chain view. */
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

    /*! \brief Lays out the hosted signal-chain view. */
    void resized() override;

private:
    // Concrete view that renders SignalChainViewState and emits signal-chain intents.
    SignalChainView m_signal_chain_view;
};

} // namespace rock_hero::editor::ui
