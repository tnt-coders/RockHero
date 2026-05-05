#include "audio_clip_view.h"

#include <rock_hero/audio/i_thumbnail.h>
#include <utility>

namespace rock_hero::ui
{

// Creates an empty clip view that will receive its thumbnail and state from a parent track view.
AudioClipView::AudioClipView() = default;

// Uses default destruction because ownership is fully represented by smart-pointer members.
AudioClipView::~AudioClipView() = default;

// Transfers thumbnail ownership into the clip view and points it at the current state asset.
void AudioClipView::setThumbnail(std::unique_ptr<audio::IThumbnail> thumbnail)
{
    m_thumbnail = std::move(thumbnail);
    m_thumbnail_source_asset.reset();
    applyCurrentAssetToThumbnailIfNeeded();
    repaint();
}

// Stores the new clip-view state, refreshes the thumbnail source when needed, and repaints.
void AudioClipView::setState(const AudioClipViewState& state)
{
    m_state = state;
    applyCurrentAssetToThumbnailIfNeeded();
    repaint();
}

// Draws one clip's waveform content while TrackView owns row-level layout and empty states.
void AudioClipView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

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

// Keeps thumbnail refresh local to the clip by diffing the current asset against the source
// already installed in this component.
void AudioClipView::applyCurrentAssetToThumbnailIfNeeded()
{
    if (!m_thumbnail)
    {
        return;
    }

    if (m_thumbnail_source_asset == m_state.asset)
    {
        return;
    }

    m_thumbnail->setSource(m_state.asset);
    m_thumbnail_source_asset = m_state.asset;
}

} // namespace rock_hero::ui
