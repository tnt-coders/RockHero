#include "track_view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/ui/audio_clip_view.h>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Maps a clip's timeline range into the visible portion of a track row.
[[nodiscard]] juce::Rectangle<int> boundsForClipTimelineRange(
    core::TimeRange clip_timeline_range, core::TimeRange visible_timeline,
    juce::Rectangle<int> row_bounds) noexcept
{
    const double visible_duration = visible_timeline.duration().seconds;
    if (row_bounds.isEmpty() || visible_duration <= 0.0)
    {
        return {};
    }

    const double start_ratio =
        (clip_timeline_range.start.seconds - visible_timeline.start.seconds) / visible_duration;
    const double end_ratio =
        (clip_timeline_range.end.seconds - visible_timeline.start.seconds) / visible_duration;
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

} // namespace

// Creates an empty track view that will receive its thumbnail factory and state from a parent view.
TrackView::TrackView() = default;

// Detaches clip child components before member ownership releases them during teardown.
TrackView::~TrackView()
{
    clearClipViews();
}

// Stores the factory used for current and future clip-view thumbnails.
void TrackView::setThumbnailFactory(audio::IThumbnailFactory& thumbnail_factory)
{
    clearClipViews();
    m_thumbnail_factory = &thumbnail_factory;
    syncClipViewsToState();
    repaint();
}

// Stores the visible timeline range and updates child clip placement from timeline coordinates.
void TrackView::setVisibleTimeline(core::TimeRange visible_timeline)
{
    m_visible_timeline = visible_timeline;
    resized();
}

// Stores the new track-view state, updates child clip views, and repaints the track-local content.
void TrackView::setState(const TrackViewState& state)
{
    m_state = state;
    syncClipViewsToState();
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

// Draws the row background and empty states; clip children render waveform content.
void TrackView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.fillAll(juce::Colours::black);

    if (m_state.audio_clips.empty())
    {
        g.setColour(juce::Colours::grey);
        const juce::String message =
            m_state.display_name.empty()
                ? "No audio loaded"
                : juce::String{m_state.display_name.c_str()} + ": no audio";
        g.drawText(message, bounds, juce::Justification::centred);
        return;
    }

    if (m_clip_views.empty())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Waveform unavailable", bounds, juce::Justification::centred);
        return;
    }
}

// Maps each owned clip child into the visible portion of the track row.
void TrackView::resized()
{
    const auto bounds = getLocalBounds();
    const std::size_t clip_count = std::min(m_clip_views.size(), m_state.audio_clips.size());
    for (std::size_t index = 0; index < clip_count; ++index)
    {
        m_clip_views[index]->setBounds(boundsForClipTimelineRange(
            m_state.audio_clips[index].timeline_range, m_visible_timeline, bounds));
    }
}

// Keeps child clip views aligned with the row state without exposing Session to JUCE components.
void TrackView::syncClipViewsToState()
{
    if (m_thumbnail_factory == nullptr)
    {
        clearClipViews();
        return;
    }

    while (m_clip_views.size() > m_state.audio_clips.size())
    {
        removeChildComponent(m_clip_views.back().get());
        m_clip_views.pop_back();
    }

    while (m_clip_views.size() < m_state.audio_clips.size())
    {
        const std::size_t index = m_clip_views.size();
        auto clip_view = std::make_unique<AudioClipView>();
        clip_view->setComponentID("audio_clip_view");
        clip_view->setState(m_state.audio_clips[index]);
        clip_view->setThumbnail(m_thumbnail_factory->createThumbnail(*clip_view));
        addAndMakeVisible(*clip_view);
        m_clip_views.push_back(std::move(clip_view));
    }

    for (std::size_t index = 0; index < m_state.audio_clips.size(); ++index)
    {
        m_clip_views[index]->setState(m_state.audio_clips[index]);
    }

    resized();
}

// Detaches owned JUCE child components before releasing the smart pointers that own them.
void TrackView::clearClipViews()
{
    while (!m_clip_views.empty())
    {
        removeChildComponent(m_clip_views.back().get());
        m_clip_views.pop_back();
    }
}

} // namespace rock_hero::ui
