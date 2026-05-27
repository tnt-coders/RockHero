#include "transport_controls.h"

#include <BinaryData.h>
#include <algorithm>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_button_size{32};
constexpr int g_button_gap{12};

} // namespace

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

    m_play_pause_button->setImages(m_play_drawable.get());
    m_play_pause_button->setComponentID("play_pause_button");
    m_play_pause_button->setClickingTogglesState(false);
    m_play_pause_button->setWantsKeyboardFocus(false);
    m_play_pause_button->setMouseClickGrabsKeyboardFocus(false);
    m_play_pause_button->setEnabled(false);

    m_stop_button->setImages(m_stop_drawable.get());
    m_stop_button->setComponentID("stop_button");
    m_stop_button->setWantsKeyboardFocus(false);
    m_stop_button->setMouseClickGrabsKeyboardFocus(false);
    m_stop_button->setEnabled(false);

    m_play_pause_button->onClick = [this] { handlePlayPauseClicked(); };
    m_stop_button->onClick = [this] { handleStopClicked(); };

    addAndMakeVisible(*m_stop_button);
    addAndMakeVisible(*m_play_pause_button);
}

// Uses default destruction because Drawable ownership is fully represented by unique_ptr members.
TransportControls::~TransportControls() = default;

// Applies already-derived enabledness and play/pause visuals without adding workflow rules.
void TransportControls::setState(const core::TransportViewState& state)
{
    m_state = state;
    const juce::Drawable* const play_pause_drawable =
        m_state.play_pause_shows_pause_icon ? m_pause_drawable.get() : m_play_drawable.get();

    m_play_pause_button->setEnabled(m_state.play_pause_enabled);
    m_play_pause_button->setImages(play_pause_drawable);
    m_play_pause_button->setToggleState(false, juce::dontSendNotification);
    m_stop_button->setEnabled(m_state.stop_enabled);
}

// Keeps fixed-size Stop and Play/Pause buttons centered in the available strip.
void TransportControls::resized()
{
    const auto area = getLocalBounds();
    const int available_button_width = std::max(0, (area.getWidth() - g_button_gap) / 2);
    const int button_size = std::min({g_button_size, area.getHeight(), available_button_width});
    if (button_size <= 0)
    {
        m_stop_button->setBounds({});
        m_play_pause_button->setBounds({});
        return;
    }

    const int group_width = button_size * 2 + g_button_gap;
    auto controls_area =
        juce::Rectangle<int>{group_width, button_size}.withCentre(area.getCentre());
    m_stop_button->setBounds(controls_area.removeFromLeft(button_size));
    controls_area.removeFromLeft(g_button_gap);
    m_play_pause_button->setBounds(controls_area.removeFromLeft(button_size));
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

} // namespace rock_hero::editor::ui
