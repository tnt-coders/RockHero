#include "arrangement_view.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>

namespace rock_hero::editor::ui
{

namespace
{

// Concrete draw request after intersecting arrangement audio with the visible timeline.
struct WaveformDrawRequest
{
    juce::Rectangle<int> bounds;
    common::core::TimeRange visible_range;
};

// Returns the overlapping portion of two timeline ranges.
[[nodiscard]] std::optional<common::core::TimeRange> intersectRanges(
    common::core::TimeRange lhs, common::core::TimeRange rhs) noexcept
{
    const common::core::TimeRange intersection{
        .start = common::core::TimePosition{std::max(lhs.start.seconds, rhs.start.seconds)},
        .end = common::core::TimePosition{std::min(lhs.end.seconds, rhs.end.seconds)},
    };

    if (intersection.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    return intersection;
}

// Maps a timeline range into the visible portion of the arrangement view.
[[nodiscard]] juce::Rectangle<int> boundsForTimelineRange(
    common::core::TimeRange timeline_range, common::core::TimeRange visible_timeline,
    juce::Rectangle<int> view_bounds) noexcept
{
    const double visible_duration = visible_timeline.duration().seconds;
    if (view_bounds.isEmpty() || visible_duration <= 0.0)
    {
        return {};
    }

    const double start_ratio =
        (timeline_range.start.seconds - visible_timeline.start.seconds) / visible_duration;
    const double end_ratio =
        (timeline_range.end.seconds - visible_timeline.start.seconds) / visible_duration;
    const double left_ratio = std::clamp(std::min(start_ratio, end_ratio), 0.0, 1.0);
    const double right_ratio = std::clamp(std::max(start_ratio, end_ratio), 0.0, 1.0);

    const auto view_width = static_cast<double>(view_bounds.getWidth());
    const int left = view_bounds.getX() + static_cast<int>(std::floor(left_ratio * view_width));
    const int right = view_bounds.getX() + static_cast<int>(std::ceil(right_ratio * view_width));

    return juce::Rectangle<int>{
        left,
        view_bounds.getY(),
        std::max(0, right - left),
        view_bounds.getHeight(),
    };
}

// Builds the draw range and bounds for the visible portion of full-source arrangement audio.
[[nodiscard]] std::optional<WaveformDrawRequest> waveformDrawRequest(
    const core::ArrangementViewState& state, common::core::TimeRange visible_timeline,
    juce::Rectangle<int> view_bounds) noexcept
{
    const common::core::TimeRange audio_timeline = state.audioTimelineRange();
    if (audio_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const common::core::TimeRange effective_visible_timeline =
        visible_timeline.duration().seconds > 0.0 ? visible_timeline : audio_timeline;
    const auto visible_audio_range = intersectRanges(audio_timeline, effective_visible_timeline);
    if (!visible_audio_range.has_value())
    {
        return std::nullopt;
    }

    return WaveformDrawRequest{
        .bounds =
            boundsForTimelineRange(*visible_audio_range, effective_visible_timeline, view_bounds),
        .visible_range = *visible_audio_range,
    };
}

} // namespace

// Creates an empty arrangement view that receives its thumbnail factory and state from a parent.
ArrangementView::ArrangementView() = default;

// Uses default destruction because ownership is fully represented by member objects.
ArrangementView::~ArrangementView() = default;

// Stores the factory and creates the arrangement-owned thumbnail bound to this component.
void ArrangementView::setThumbnailFactory(common::audio::IThumbnailFactory& thumbnail_factory)
{
    m_thumbnail = thumbnail_factory.createThumbnail(*this);
    m_thumbnail_source_asset.reset();
    applyCurrentAudioToThumbnailIfNeeded();
    repaint();
}

// Stores the visible timeline range and redraws the appropriate waveform subsection.
void ArrangementView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    m_visible_timeline = visible_timeline;
    repaint();
}

// Stores the new arrangement-view state, refreshes the thumbnail source, and repaints.
void ArrangementView::setState(const core::ArrangementViewState& state)
{
    m_state = state;
    applyCurrentAudioToThumbnailIfNeeded();
    repaint();
}

// Registers a local click listener for normalized arrangement-view intent.
void ArrangementView::addListener(Listener& listener)
{
    m_listeners.add(&listener);
}

// Removes a previously registered local click listener.
void ArrangementView::removeListener(Listener& listener)
{
    m_listeners.remove(&listener);
}

// Converts arrangement-view clicks into normalized horizontal intent and leaves seek policy to the
// parent.
void ArrangementView::mouseDown(const juce::MouseEvent& event)
{
    if (getWidth() <= 0)
    {
        return;
    }

    const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    m_listeners.call(&Listener::arrangementViewClicked, *this, clamped);
}

// Draws the view background, status text, and the currently visible waveform range.
void ArrangementView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (!m_state.hasAudio())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("No audio loaded", bounds, juce::Justification::centred);
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

    const auto draw_request = waveformDrawRequest(m_state, m_visible_timeline, bounds);
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
void ArrangementView::resized()
{
    repaint();
}

// Keeps thumbnail refresh local by diffing the current asset against the source
// already installed in this component.
void ArrangementView::applyCurrentAudioToThumbnailIfNeeded()
{
    if (!m_thumbnail)
    {
        return;
    }

    if (!m_state.audio_asset.has_value())
    {
        m_thumbnail_source_asset.reset();
        return;
    }

    if (m_thumbnail_source_asset == m_state.audio_asset)
    {
        return;
    }

    m_thumbnail->setSource(*m_state.audio_asset);
    m_thumbnail_source_asset = m_state.audio_asset;
}

} // namespace rock_hero::editor::ui
