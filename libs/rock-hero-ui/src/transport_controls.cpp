#include "transport_controls.h"

#include <BinaryData.h>

namespace rock_hero::ui
{

// Creates icon buttons from embedded SVGs and wires them to local listener-based transport
// intents.
TransportControls::TransportControls(Listener& listener)
    : m_listener(listener)
    , m_play_pause_button(
          std::make_unique<juce::DrawableButton>("play_pause", juce::DrawableButton::ImageFitted))
    , m_stop_button(
          std::make_unique<juce::DrawableButton>("stop", juce::DrawableButton::ImageFitted))
{
    m_play_drawable = juce::Drawable::createFromImageData(
        BinaryData::play_arrow_svg, BinaryData::play_arrow_svgSize);
    m_pause_drawable =
        juce::Drawable::createFromImageData(BinaryData::pause_svg, BinaryData::pause_svgSize);
    m_stop_drawable =
        juce::Drawable::createFromImageData(BinaryData::stop_svg, BinaryData::stop_svgSize);

    m_play_pause_button->setImages(
        m_play_drawable.get(), nullptr, nullptr, nullptr, m_pause_drawable.get());
    m_play_pause_button->setClickingTogglesState(false);
    m_play_pause_button->setEnabled(false);

    m_stop_button->setImages(m_stop_drawable.get());
    m_stop_button->setEnabled(false);

    m_play_pause_button->onClick = [this] { handlePlayPauseClicked(); };
    m_stop_button->onClick = [this] { handleStopClicked(); };

    addAndMakeVisible(*m_play_pause_button);
    addAndMakeVisible(*m_stop_button);
}

// Uses default destruction because Drawable ownership is fully represented by unique_ptr members.
TransportControls::~TransportControls() = default;

// Applies already-derived enabledness and play/pause visuals without adding workflow rules.
void TransportControls::setState(const TransportControlsState& state)
{
    m_state = state;
    m_play_pause_button->setEnabled(m_state.play_pause_enabled);
    m_play_pause_button->setToggleState(
        m_state.play_pause_shows_pause_icon, juce::dontSendNotification);
    m_stop_button->setEnabled(m_state.stop_enabled);
}

// Keeps play/pause and stop buttons equal-width with a fixed gap.
void TransportControls::resized()
{
    auto area = getLocalBounds();
    const int button_width = (area.getWidth() - 4) / 2;
    m_play_pause_button->setBounds(area.removeFromLeft(button_width));
    area.removeFromLeft(4);
    m_stop_button->setBounds(area);
}

// Forwards play/pause button clicks to the parent listener that owns transport semantics.
void TransportControls::handlePlayPauseClicked()
{
    m_listener.onPlayPausePressed();
}

// Forwards stop button clicks to the parent listener that owns transport semantics.
void TransportControls::handleStopClicked()
{
    m_listener.onStopPressed();
}

} // namespace rock_hero::ui
