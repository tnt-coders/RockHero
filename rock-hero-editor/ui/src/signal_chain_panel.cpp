#include "signal_chain_panel.h"

namespace rock_hero::editor::ui
{

// Creates the panel wrapper and installs the state-rendering signal-chain view.
SignalChainPanel::SignalChainPanel(Listener& listener)
    : m_signal_chain_view(listener)
{
    setComponentID("signal_chain_panel");
    addAndMakeVisible(m_signal_chain_view);
}

SignalChainPanel::~SignalChainPanel() = default;

// Delegates controller-derived render state to the hosted view.
void SignalChainPanel::setState(const core::SignalChainViewState& state)
{
    m_signal_chain_view.setState(state);
}

// Delegates continuously sampled live-rig meters to the hosted view.
void SignalChainPanel::setMeterLevels(
    common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level)
{
    m_signal_chain_view.setMeterLevels(input_level, output_level);
}

// Keeps the hosted view exactly aligned with the editor-owned panel region.
void SignalChainPanel::resized()
{
    m_signal_chain_view.setBounds(getLocalBounds());
}

} // namespace rock_hero::editor::ui
