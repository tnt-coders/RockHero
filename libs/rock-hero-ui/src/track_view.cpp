#include "track_view.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>

namespace rock_hero::ui
{

namespace
{

// Concrete draw request after intersecting the track audio with the visible timeline.
struct WaveformDrawRequest
{
    juce::Rectangle<int> bounds;
    core::TimeRange visible_range;
};

// Returns the overlapping portion of two timeline ranges.
[[nodiscard]] std::optional<core::TimeRange> intersectRanges(
    core::TimeRange lhs, core::TimeRange rhs) noexcept
{
    const core::TimeRange intersection{
        .start = core::TimePosition{std::max(lhs.start.seconds, rhs.start.seconds)},
        .end = core::TimePosition{std::min(lhs.end.seconds, rhs.end.seconds)},
    };

    if (intersection.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    return intersection;
}

// Maps a timeline range into the visible portion of a track row.
[[nodiscard]] juce::Rectangle<int> boundsForTimelineRange(
    core::TimeRange timeline_range, core::TimeRange visible_timeline,
    juce::Rectangle<int> row_bounds) noexcept
{
    const double visible_duration = visible_timeline.duration().seconds;
    if (row_bounds.isEmpty() || visible_duration <= 0.0)
    {
        return {};
    }

    const double start_ratio =
        (timeline_range.start.seconds - visible_timeline.start.seconds) / visible_duration;
    const double end_ratio =
        (timeline_range.end.seconds - visible_timeline.start.seconds) / visible_duration;
    const double left_ratio = std::clamp(std::min(start_ratio, end_ratio), 0.0, 1.0);
    const double right_ratio = std::clamp(std::max(start_ratio, end_ratio), 0.0, 1.0);

    const double row_width = static_cast<double>(row_bounds.getWidth());
    const int left = row_bounds.getX() + static_cast<int>(std::floor(left_ratio * row_width));
    const int right = row_bounds.getX() + static_cast<int>(std::ceil(right_ratio * row_width));

    return juce::Rectangle<int>{
        left,
        row_bounds.getY(),
        std::max(0, right - left),
        row_bounds.getHeight(),
    };
}

// Builds the draw range and bounds for the currently visible portion of full-source track audio.
[[nodiscard]] std::optional<WaveformDrawRequest> waveformDrawRequest(
    const core::TrackAudio& audio, core::TimeRange visible_timeline,
    juce::Rectangle<int> row_bounds) noexcept
{
    const core::TimeRange audio_timeline = audio.timelineRange();
    if (audio_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const core::TimeRange effective_visible_timeline =
        visible_timeline.duration().seconds > 0.0 ? visible_timeline : audio_timeline;
    const auto visible_audio_range = intersectRanges(audio_timeline, effective_visible_timeline);
    if (!visible_audio_range.has_value())
    {
        return std::nullopt;
    }

    return WaveformDrawRequest{
        .bounds =
            boundsForTimelineRange(*visible_audio_range, effective_visible_timeline, row_bounds),
        .visible_range = *visible_audio_range,
    };
}

} // namespace

// Creates an empty track view that will receive its thumbnail factory and state from a parent view.
TrackView::TrackView() = default;

// Uses default destruction because ownership is fully represented by member objects.
TrackView::~TrackView() = default;

// Stores the factory and creates the track-owned thumbnail bound to this component.
void TrackView::setThumbnailFactory(audio::IThumbnailFactory& thumbnail_factory)
{
    m_thumbnail = thumbnail_factory.createThumbnail(*this);
    m_thumbnail_source_asset.reset();
    applyCurrentAudioToThumbnailIfNeeded();
    repaint();
}

// Stores the visible timeline range and redraws the appropriate waveform subsection.
void TrackView::setVisibleTimeline(core::TimeRange visible_timeline)
{
    m_visible_timeline = visible_timeline;
    repaint();
}

// Stores the new track-view state, refreshes the thumbnail source when needed, and repaints.
void TrackView::setState(const TrackViewState& state)
{
    m_state = state;
    applyCurrentAudioToThumbnailIfNeeded();
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

// Draws the row background, status text, and the currently visible waveform range.
void TrackView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (!m_state.audio.has_value())
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

    if (!m_thumbnail->hasSource())
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

    const auto draw_request = waveformDrawRequest(*m_state.audio, m_visible_timeline, bounds);
    if (!draw_request.has_value() || draw_request->bounds.isEmpty())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Waveform unavailable", bounds, juce::Justification::centred);
        return;
    }

    g.setColour(juce::Colours::lightgreen);
    if (!m_thumbnail->drawChannels(g, draw_request->bounds, draw_request->visible_range, 1.0f))
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Waveform unavailable", bounds, juce::Justification::centred);
    }
}

// Size changes only affect paint-time waveform mapping, so invalidating the row is enough.
void TrackView::resized()
{
    repaint();
}

// Keeps thumbnail refresh local to the track by diffing the current asset against the source
// already installed in this component.
void TrackView::applyCurrentAudioToThumbnailIfNeeded()
{
    if (!m_thumbnail)
    {
        return;
    }

    if (!m_state.audio.has_value())
    {
        m_thumbnail_source_asset.reset();
        return;
    }

    if (m_thumbnail_source_asset == m_state.audio->asset)
    {
        return;
    }

    m_thumbnail->setSource(m_state.audio->asset);
    m_thumbnail_source_asset = m_state.audio->asset;
}

} // namespace rock_hero::ui
