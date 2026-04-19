#include "transport_controls.h"

#include <BinaryData.h>

namespace rock_hero::ui
{

// Creates icon buttons from embedded SVGs and wires them to callback-based transport intents.
TransportControls::TransportControls()
    : m_play_pause_button(
          std::make_unique<juce::DrawableButton>("play_pause", juce::DrawableButton::ImageFitted)),
      m_stop_button(
          std::make_unique<juce::DrawableButton>("stop", juce::DrawableButton::ImageFitted))
{
    m_play_drawable = juce::Drawable::createFromImageData(
        BinaryData::play_arrow_svg, BinaryData::play_arrow_svgSize);
    m_pause_drawable =
        juce::Drawable::createFromImageData(BinaryData::pause_svg, BinaryData::pause_svgSize);
    m_stop_drawable =
        juce::Drawable::createFromImageData(BinaryData::stop_svg, BinaryData::stop_svgSize);

    m_play_pause_button->setImages(m_play_drawable.get());
    m_play_pause_button->setEnabled(false);

    m_stop_button->setImages(m_stop_drawable.get());
    m_stop_button->setEnabled(false);

    m_play_pause_button->onClick = [this] { onPlayPauseClicked(); };
    m_stop_button->onClick = [this] {
        if (on_stop)
        {
            on_stop();
        }
    };

    addAndMakeVisible(*m_play_pause_button);
    addAndMakeVisible(*m_stop_button);
}

// Uses default destruction because Drawable ownership is fully represented by unique_ptr members.
TransportControls::~TransportControls() = default;

// Mirrors engine playback state into the visible play/pause icon.
void TransportControls::setPlaying(bool playing)
{
    m_is_playing = playing;
    m_play_pause_button->setImages(playing ? m_pause_drawable.get() : m_play_drawable.get());
}

// Enables transport actions only after the editor has successfully loaded an audio file.
void TransportControls::setFileLoaded(bool loaded)
{
    m_file_loaded = loaded;
    m_play_pause_button->setEnabled(loaded);
    m_stop_button->setEnabled(loaded);
}

// Lets keyboard handlers share the same loaded-file guard as the visible controls.
bool TransportControls::isFileLoaded() const
{
    return m_file_loaded;
}

// Emits the correct transport intent for both button clicks and Space-bar toggles.
void TransportControls::onPlayPauseClicked()
{
    if (m_is_playing)
    {
        if (on_pause)
        {
            on_pause();
        }
    }
    else
    {
        if (on_play)
        {
            on_play();
        }
    }
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

} // namespace rock_hero::ui
