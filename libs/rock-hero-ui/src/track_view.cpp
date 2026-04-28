#include "track_view.h"

#include <algorithm>
#include <rock_hero/audio/i_thumbnail.h>
#include <utility>

namespace rock_hero::ui
{

// Creates an empty track view that will receive its thumbnail and state from a parent view.
TrackView::TrackView() = default;

// Uses default destruction because ownership is fully represented by smart-pointer members.
TrackView::~TrackView() = default;

// Transfers thumbnail ownership into the view and points it at the current state asset when needed.
void TrackView::setThumbnail(std::unique_ptr<audio::IThumbnail> thumbnail)
{
    m_thumbnail = std::move(thumbnail);
    m_thumbnail_source_asset.reset();
    applyCurrentAssetToThumbnailIfNeeded();
    repaint();
}

// Stores the new track-view state, refreshes the thumbnail source if the present asset changed,
// and repaints the track-local content.
void TrackView::setState(const TrackViewState& state)
{
    m_state = state;
    applyCurrentAssetToThumbnailIfNeeded();
    repaint();
}

// Registers a local click listener for normalized track-view intent.
void TrackView::addListener(Listener& listener)
{
    m_listeners.add(&listener);
}

// Removes a previously registered local click listener.
void TrackView::removeListener(Listener& listener)
{
    m_listeners.remove(&listener);
}

// Converts track-view clicks into normalized horizontal intent and leaves seek policy to the
// parent.
void TrackView::mouseDown(const juce::MouseEvent& event)
{
    if (getWidth() <= 0)
    {
        return;
    }

    const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    m_listeners.call(&Listener::trackViewClicked, *this, clamped);
}

// Draws only the track-local waveform content; cursor motion is intentionally excluded.
void TrackView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (!m_state.audio_asset.has_value())
    {
        g.setColour(juce::Colours::grey);
        const juce::String message =
            m_state.display_name.empty()
                ? "No audio loaded"
                : juce::String{m_state.display_name.c_str()} + ": no audio";
        g.drawText(message, bounds, juce::Justification::centred);
        return;
    }

    if (!m_thumbnail)
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Waveform unavailable", bounds, juce::Justification::centred);
        return;
    }

    if (m_thumbnail->getLength() <= 0.0)
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Preparing waveform", bounds, juce::Justification::centred);
        return;
    }

    if (m_thumbnail->isGeneratingProxy())
    {
        const auto pct = static_cast<int>(m_thumbnail->getProxyProgress() * 100.0f);
        g.setColour(juce::Colours::white);
        g.drawText(
            "Building waveform: " + juce::String{pct} + "%", bounds, juce::Justification::centred);
        return;
    }

    g.setColour(juce::Colours::lightgreen);
    m_thumbnail->drawChannels(g, bounds, 1.0f);
}

// Documents the intentional absence of child layout while preserving the JUCE override point.
void TrackView::resized()
{
    // The view renders directly into its own bounds; no child components exist at this stage.
}

// Keeps thumbnail refresh local to the view by diffing the current present asset against the
// thumbnail source already installed in this component.
void TrackView::applyCurrentAssetToThumbnailIfNeeded()
{
    if (!m_thumbnail || !m_state.audio_asset.has_value())
    {
        return;
    }

    if (m_thumbnail_source_asset == m_state.audio_asset)
    {
        return;
    }

    m_thumbnail->setSource(*m_state.audio_asset);
    m_thumbnail_source_asset = m_state.audio_asset;
}

} // namespace rock_hero::ui
