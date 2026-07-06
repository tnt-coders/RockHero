#include "tab/tab_view.h"

#include "shared/text_metrics.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// Rocksmith's six standard string colors, ordered from the sixth-highest displayed lane upward:
// primaries in RYB derivation order, interleaved with secondaries for adjacent contrast.
constexpr std::array<juce::uint32, 6> g_standard_string_colors{
    0xffe23c37, // red (low E on a standard guitar)
    0xfff0c841, // yellow (A)
    0xff4682eb, // blue (D)
    0xfff08c2d, // orange (G)
    0xff5fc85a, // green (B)
    0xffaa5fdc, // purple (high e)
};

// RYB tertiary tier for lanes below the standard window, ordered going down and cycled for
// extreme lane counts; teal beside the red low E mirrors the standard set's adjacent contrast.
constexpr std::array<juce::uint32, 4> g_tertiary_string_colors{
    0xff00b5a0, // teal (7th string)
    0xffff0090, // magenta (8th)
    0xffaadc00, // chartreuse (9th)
    0xff5854ff, // indigo (10th)
};

// Note heads keep a readable fixed pixel size instead of scaling with zoom; below the text
// threshold the head collapses to a bare marker.
constexpr int g_max_head_height{20};
constexpr int g_min_head_height_for_text{9};
constexpr float g_head_corner_fraction{0.25f};
constexpr int g_head_text_pad{6};
constexpr float g_min_head_aspect{1.3f};

// Maps a timeline time onto the component's horizontal axis.
[[nodiscard]] float xForSeconds(
    double seconds, common::core::TimeRange visible_timeline, int width) noexcept
{
    const double duration = visible_timeline.duration().seconds;
    return static_cast<float>(
        (seconds - visible_timeline.start.seconds) / duration * static_cast<double>(width));
}

} // namespace

// The chart's string count floors the lane count so a user minimum can only add empty lanes.
int tabDisplayedStringCount(int chart_string_count, int minimum_displayed_strings) noexcept
{
    if (chart_string_count <= 0)
    {
        return 0;
    }

    return std::max(chart_string_count, minimum_displayed_strings);
}

// The six highest lanes take the standard set; lower lanes walk the tertiary tier downward.
juce::Colour tabStringColor(int displayed_string, int displayed_string_count) noexcept
{
    const int below_standard_window = std::max(0, displayed_string_count - 6);
    if (displayed_string > below_standard_window)
    {
        const auto standard_index =
            static_cast<std::size_t>(displayed_string - below_standard_window - 1);
        return juce::Colour{
            g_standard_string_colors[std::min(standard_index, g_standard_string_colors.size() - 1)]
        };
    }

    const auto tertiary_index = static_cast<std::size_t>(below_standard_window - displayed_string);
    return juce::Colour{g_tertiary_string_colors[tertiary_index % g_tertiary_string_colors.size()]};
}

// Standard tablature orientation: highest string on top, lowest on the bottom.
float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    const float lane_height =
        static_cast<float>(bounds.getHeight()) / static_cast<float>(displayed_string_count);
    const auto lane_index = static_cast<float>(displayed_string_count - displayed_string);
    return static_cast<float>(bounds.getY()) + (lane_index + 0.5f) * lane_height;
}

// Sorted starts bound the range's end; the non-decreasing prefix maximum of sustain ends bounds
// its start, because every note before the first index whose running maximum reaches the span
// ends strictly before the span.
std::pair<std::size_t, std::size_t> tabVisibleNoteRange(
    const std::vector<core::TabNoteView>& notes, const std::vector<double>& prefix_max_end_seconds,
    double span_start_seconds, double span_end_seconds) noexcept
{
    const auto begin_it = std::ranges::lower_bound(prefix_max_end_seconds, span_start_seconds);
    const auto end_it = std::ranges::upper_bound(
        notes, span_end_seconds, std::ranges::less{}, [](const core::TabNoteView& note) {
            return note.start_seconds;
        });

    const auto first =
        static_cast<std::size_t>(std::distance(prefix_max_end_seconds.begin(), begin_it));
    const auto last = static_cast<std::size_t>(std::distance(notes.begin(), end_it));
    return {std::min(first, last), last};
}

// Pointer events fall through to the cursor overlay's click-to-seek handling.
TabView::TabView()
{
    setInterceptsMouseClicks(false, false);
}

// Stores the visible timeline range used to map note times to pixels.
void TabView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }

    m_visible_timeline = visible_timeline;
    repaint();
}

// Applies the current tab projection and lane-count preference; the projection pointer only
// changes when the displayed arrangement changes, so pointer identity gates the index rebuild.
void TabView::setState(std::shared_ptr<const core::TabViewState> tab, int minimum_displayed_strings)
{
    const bool tab_changed = tab != m_tab;
    const bool lanes_changed = minimum_displayed_strings != m_minimum_displayed_strings;
    if (!tab_changed && !lanes_changed)
    {
        return;
    }

    m_tab = std::move(tab);
    m_minimum_displayed_strings = minimum_displayed_strings;
    if (tab_changed)
    {
        rebuildVisibilityIndex();
    }

    repaint();
}

// Draws the visible notes and sustains onto the lane, clipped to the repaint span.
void TabView::paint(juce::Graphics& g)
{
    if (m_tab == nullptr || m_tab->string_count <= 0)
    {
        return;
    }

    const juce::Rectangle<int> bounds = getLocalBounds();
    const double duration = m_visible_timeline.duration().seconds;
    if (bounds.isEmpty() || duration <= 0.0)
    {
        return;
    }

    const int displayed_count =
        tabDisplayedStringCount(m_tab->string_count, m_minimum_displayed_strings);
    const float lane_height =
        static_cast<float>(bounds.getHeight()) / static_cast<float>(displayed_count);
    const float head_height =
        std::clamp(lane_height - 4.0f, 4.0f, static_cast<float>(g_max_head_height));
    const bool draw_fret_text = head_height >= static_cast<float>(g_min_head_height_for_text);
    const juce::Font fret_font{juce::FontOptions{std::max(8.0f, head_height - 4.0f)}};

    // Heads extend a fixed pixel slack left and right of their onset, so the visible span grows
    // by that slack before the note-range query.
    const juce::Rectangle<int> clip = g.getClipBounds();
    const double seconds_per_pixel = duration / static_cast<double>(bounds.getWidth());
    const double slack_seconds = static_cast<double>(g_max_head_height) * 2.0 * seconds_per_pixel;
    const double span_start = m_visible_timeline.start.seconds +
                              static_cast<double>(clip.getX()) * seconds_per_pixel - slack_seconds;
    const double span_end = m_visible_timeline.start.seconds +
                            static_cast<double>(clip.getRight()) * seconds_per_pixel +
                            slack_seconds;

    const auto [first, last] =
        tabVisibleNoteRange(m_tab->notes, m_prefix_max_end_seconds, span_start, span_end);

    // The displayed lanes place the chart's strings on top and any extra user lanes below.
    const int extra_lanes = displayed_count - m_tab->string_count;
    const float sustain_height = std::max(2.0f, head_height / 3.0f);

    for (std::size_t index = first; index < last; ++index)
    {
        const core::TabNoteView& note = m_tab->notes[index];
        if (note.end_seconds < span_start)
        {
            continue;
        }

        const int displayed_string = note.string + extra_lanes;
        const float center_y = tabLaneCenterY(displayed_string, displayed_count, bounds);
        const float onset_x =
            xForSeconds(note.start_seconds, m_visible_timeline, bounds.getWidth());
        const juce::Colour string_color = tabStringColor(displayed_string, displayed_count);

        if (note.end_seconds > note.start_seconds)
        {
            const float end_x =
                xForSeconds(note.end_seconds, m_visible_timeline, bounds.getWidth());
            const juce::Rectangle<float> tail{
                onset_x, center_y - sustain_height / 2.0f, end_x - onset_x, sustain_height
            };
            g.setColour(string_color.withAlpha(0.75f));
            g.fillRoundedRectangle(tail, sustain_height / 2.0f);
        }

        const juce::String fret_text{note.fret};
        const float text_width =
            draw_fret_text ? static_cast<float>(textWidth(fret_font, fret_text)) : 0.0f;
        const float head_width = std::max(
            head_height * g_min_head_aspect, text_width + static_cast<float>(g_head_text_pad));
        const juce::Rectangle<float> head{
            onset_x - head_width / 2.0f, center_y - head_height / 2.0f, head_width, head_height
        };
        g.setColour(string_color);
        g.fillRoundedRectangle(head, head_height * g_head_corner_fraction);

        if (draw_fret_text)
        {
            // Contrast against the lane color, not a fixed ink: yellow and chartreuse lanes need
            // dark numerals where the darker lanes need light ones.
            g.setColour(
                string_color.getPerceivedBrightness() > 0.6f ? juce::Colours::black
                                                             : juce::Colours::white);
            g.setFont(fret_font);
            g.drawText(fret_text, head, juce::Justification::centred);
        }
    }
}

// Rebuilds the prefix-maximum sustain-end table after the projection changes.
void TabView::rebuildVisibilityIndex()
{
    m_prefix_max_end_seconds.clear();
    if (m_tab == nullptr)
    {
        return;
    }

    m_prefix_max_end_seconds.reserve(m_tab->notes.size());
    double running_max = -std::numeric_limits<double>::infinity();
    for (const core::TabNoteView& note : m_tab->notes)
    {
        running_max = std::max(running_max, note.end_seconds);
        m_prefix_max_end_seconds.push_back(running_max);
    }
}

} // namespace rock_hero::editor::ui
