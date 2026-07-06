#include "tab/tab_view.h"

#include "shared/text_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// Layout facts shared by every glyph pass of one paint call.
struct TabLaneMetrics
{
    juce::Rectangle<int> bounds;
    common::core::TimeRange visible_timeline;
    int displayed_count{};
    int extra_lanes{};
    float lane_height{};
    float head_height{};
    float sustain_height{};
    bool draw_text{};
    juce::Font fret_font;
    juce::Font badge_font;

    // Maps a timeline time onto the lane's horizontal axis.
    [[nodiscard]] float x(double seconds) const noexcept
    {
        return xForSeconds(seconds, visible_timeline, bounds.getWidth());
    }

    // Vertical lane center for a chart string, accounting for extra user lanes below the chart.
    [[nodiscard]] float laneY(int chart_string) const noexcept
    {
        return tabLaneCenterY(chart_string + extra_lanes, displayed_count, bounds);
    }

    // Lane color for a chart string, accounting for extra user lanes below the chart.
    [[nodiscard]] juce::Colour laneColor(int chart_string) const
    {
        return tabStringColor(chart_string + extra_lanes, displayed_count);
    }
};

// Ink that stays readable over a lane color: dark on the bright yellow/chartreuse lanes.
[[nodiscard]] juce::Colour contrastInk(juce::Colour lane_color)
{
    return lane_color.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white;
}

// Guitarist-facing bend amount: two semitones make one whole-step "full" bend, and the corpus
// uses quarter-step curls, so amounts render as quarters of a step with unicode fractions.
[[nodiscard]] juce::String bendLabelText(double semitones)
{
    const auto quarter_steps = static_cast<int>(std::lround(semitones * 2.0));
    if (quarter_steps <= 0 || std::abs(semitones * 2.0 - quarter_steps) > 0.01)
    {
        return juce::String{semitones / 2.0, 2};
    }

    constexpr std::array<const char*, 4> quarter_fragments{"", "\xC2\xBC", "\xC2\xBD", "\xC2\xBE"};
    const int whole_steps = quarter_steps / 4;
    const int remainder = quarter_steps % 4;
    juce::String fragment{juce::CharPointer_UTF8{quarter_fragments.at(
        static_cast<std::size_t>(remainder))}};
    if (whole_steps == 0)
    {
        return fragment;
    }
    if (whole_steps == 1 && remainder == 0)
    {
        return "full";
    }
    return juce::String{whole_steps} + fragment;
}

// Draws the plain sustain bar, or its vibrato/tremolo variants, from the head to the note end.
// Tremolo fills a zigzag band clipped to the exact sustain rectangle so the strip's edges stay
// straight instead of ending mid-tooth, and the vibrato sine starts and stops exactly at the
// sustain bounds by construction.
void drawSustainTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float onset_x,
    float center_y, juce::Colour lane_color)
{
    const float end_x = metrics.x(note.end_seconds);
    if (end_x <= onset_x)
    {
        return;
    }

    if (note.tremolo)
    {
        const float band_height = std::max(4.0f, metrics.head_height * 0.6f);
        const juce::Rectangle<float> band{
            onset_x, center_y - band_height / 2.0f, end_x - onset_x, band_height
        };
        juce::Path teeth;
        constexpr float tooth_width = 6.0f;
        teeth.startNewSubPath(onset_x, band.getBottom());
        const auto tooth_count =
            static_cast<int>((end_x + tooth_width - onset_x) / tooth_width) + 1;
        for (int tooth = 0; tooth < tooth_count; ++tooth)
        {
            const float x = onset_x + static_cast<float>(tooth) * tooth_width;
            teeth.lineTo(x, tooth % 2 == 0 ? band.getY() : band.getBottom());
        }
        teeth.lineTo(end_x + tooth_width, band.getBottom());
        teeth.closeSubPath();

        g.saveState();
        g.reduceClipRegion(band.getSmallestIntegerContainer());
        g.setColour(lane_color.withAlpha(0.75f));
        g.fillPath(teeth);
        g.restoreState();
        return;
    }

    if (note.vibrato)
    {
        const float amplitude = std::max(2.0f, metrics.head_height * 0.3f);
        constexpr float wavelength = 10.0f;
        juce::Path wave;
        wave.startNewSubPath(onset_x, center_y);
        const auto wave_pixels = static_cast<int>(end_x - onset_x);
        for (int step = 1; step <= wave_pixels; ++step)
        {
            const float x = onset_x + static_cast<float>(step);
            const float phase = (x - onset_x) / wavelength * juce::MathConstants<float>::twoPi;
            wave.lineTo(x, center_y + amplitude * std::sin(phase));
        }
        wave.lineTo(end_x, center_y);
        g.setColour(lane_color.withAlpha(0.9f));
        g.strokePath(wave, juce::PathStrokeType{2.0f});
        return;
    }

    const juce::Rectangle<float> tail{
        onset_x, center_y - metrics.sustain_height / 2.0f, end_x - onset_x, metrics.sustain_height
    };
    g.setColour(lane_color.withAlpha(0.75f));
    g.fillRoundedRectangle(tail, metrics.sustain_height / 2.0f);
}

// Draws the bend curve rising above the lane center with a dot and amount label per bend point.
void drawBendCurve(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float onset_x,
    float center_y)
{
    if (note.bend.empty())
    {
        return;
    }

    // A full-step bend rises most of a lane; larger bends clamp so curves never leave the lane
    // band entirely.
    const auto rise_for = [&](double semitones) {
        return static_cast<float>(std::min(semitones, 4.0)) / 4.0f * metrics.lane_height * 0.9f;
    };

    juce::Path curve;
    curve.startNewSubPath(onset_x, center_y);
    for (const core::TabBendPointView& point : note.bend)
    {
        curve.lineTo(metrics.x(point.seconds), center_y - rise_for(point.semitones));
    }
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.strokePath(curve, juce::PathStrokeType{1.5f});

    for (const core::TabBendPointView& point : note.bend)
    {
        const float point_x = metrics.x(point.seconds);
        const float point_y = center_y - rise_for(point.semitones);
        g.fillEllipse(point_x - 2.0f, point_y - 2.0f, 4.0f, 4.0f);
        if (metrics.draw_text && point.semitones > 0.0)
        {
            g.setFont(metrics.badge_font);
            g.drawText(
                bendLabelText(point.semitones),
                juce::Rectangle<float>{point_x + 3.0f, point_y - 12.0f, 36.0f, 12.0f},
                juce::Justification::centredLeft);
        }
    }
}

// Draws slide lines from the note head through each waypoint, with the target fret at each stop.
// Unpitched slides draw dashed lines and skip the target box, reading as a fall-off.
void drawSlideWaypoints(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float onset_x,
    float center_y, juce::Colour lane_color)
{
    if (note.slides.empty())
    {
        return;
    }

    const float step = metrics.head_height * 0.35f;
    juce::Point<float> from{onset_x, center_y};
    int previous_fret = note.fret;
    for (const core::TabSlideView& waypoint : note.slides)
    {
        const float target_x = metrics.x(waypoint.seconds);
        const float target_y = waypoint.fret >= previous_fret ? center_y - step : center_y + step;
        const juce::Line<float> line{from.x, from.y, target_x, target_y};
        g.setColour(lane_color);
        if (waypoint.unpitched)
        {
            constexpr std::array<float, 2> dashes{4.0f, 3.0f};
            g.drawDashedLine(line, dashes.data(), static_cast<int>(dashes.size()), 1.5f);
        }
        else
        {
            g.drawLine(line, 2.0f);
        }

        if (metrics.draw_text)
        {
            g.setFont(metrics.badge_font);
            g.drawText(
                juce::String{waypoint.fret},
                juce::Rectangle<float>{target_x - 12.0f, target_y - 14.0f, 24.0f, 12.0f},
                juce::Justification::centred);
        }

        from = {target_x, target_y};
        previous_fret = waypoint.fret;
    }
}

// Draws the note head: a diamond for natural harmonics, a rounded rectangle otherwise, with the
// fret number (or a muted "X") inside and an accent ring when marked.
void drawNoteHead(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float onset_x,
    float center_y, juce::Colour lane_color)
{
    const bool full_mute = note.mute == common::core::NoteMute::Full;
    const juce::String head_text = full_mute ? juce::String{"X"} : juce::String{note.fret};
    const float text_width =
        metrics.draw_text ? static_cast<float>(textWidth(metrics.fret_font, head_text)) : 0.0f;
    const float head_width = std::max(
        metrics.head_height * g_min_head_aspect, text_width + static_cast<float>(g_head_text_pad));
    const juce::Rectangle<float> head{
        onset_x - head_width / 2.0f,
        center_y - metrics.head_height / 2.0f,
        head_width,
        metrics.head_height
    };

    juce::Path head_shape;
    if (note.harmonic == common::core::NoteHarmonic::Natural)
    {
        // Natural harmonics take Rocksmith's diamond silhouette; widen the diamond so its
        // midsection still fits the fret number.
        const float diamond_width = head_width * 1.6f;
        head_shape.addQuadrilateral(
            onset_x - diamond_width / 2.0f,
            center_y,
            onset_x,
            head.getY(),
            onset_x + diamond_width / 2.0f,
            center_y,
            onset_x,
            head.getBottom());
    }
    else
    {
        head_shape.addRoundedRectangle(head, metrics.head_height * g_head_corner_fraction);
    }

    g.setColour(lane_color);
    g.fillPath(head_shape);

    // Pinch harmonics squeal over any attack, so they mark the head itself with a bright rim
    // (plus the PH badge) rather than replacing its shape.
    if (note.harmonic == common::core::NoteHarmonic::Pinch)
    {
        g.setColour(juce::Colours::white);
        g.strokePath(head_shape, juce::PathStrokeType{1.5f});
    }

    if (note.accent)
    {
        g.setColour(contrastInk(lane_color).withAlpha(0.9f));
        g.strokePath(head_shape, juce::PathStrokeType{2.0f});
    }

    if (metrics.draw_text)
    {
        g.setColour(contrastInk(lane_color));
        g.setFont(metrics.fret_font);
        g.drawText(head_text, head, juce::Justification::centred);
    }
}

// Draws the technique badges above the head: attack letters plus palm-mute and pinch-harmonic
// markers, joined in one short line so simultaneous techniques stay legible.
void drawTechniqueBadges(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float onset_x,
    float center_y)
{
    if (!metrics.draw_text)
    {
        return;
    }

    juce::StringArray badges;
    switch (note.attack)
    {
        case common::core::NoteAttack::Hammer:
        {
            badges.add("H");
            break;
        }
        case common::core::NoteAttack::Pull:
        {
            badges.add("P");
            break;
        }
        case common::core::NoteAttack::Tap:
        {
            badges.add("T");
            break;
        }
        case common::core::NoteAttack::Slap:
        {
            badges.add("SL");
            break;
        }
        case common::core::NoteAttack::Pop:
        {
            badges.add("PO");
            break;
        }
        case common::core::NoteAttack::Pick:
        {
            break;
        }
    }
    if (note.mute == common::core::NoteMute::Palm)
    {
        badges.add("PM");
    }
    if (note.harmonic == common::core::NoteHarmonic::Pinch)
    {
        badges.add("PH");
    }
    if (badges.isEmpty())
    {
        return;
    }

    const juce::String text = badges.joinIntoString(" ");
    const float badge_height = metrics.badge_font.getHeight();
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(metrics.badge_font);
    g.drawText(
        text,
        juce::Rectangle<float>{
            onset_x - 30.0f,
            center_y - metrics.head_height / 2.0f - badge_height - 1.0f,
            60.0f,
            badge_height
        },
        juce::Justification::centred);
}

// Draws one chord-shape span: strummed shapes read as a tinted box with a border, arpeggios as
// a dashed outline only, both labeled with the template name when text fits.
void drawShapeSpan(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabShapeView& shape)
{
    const float start_x = metrics.x(shape.start_seconds);
    const float end_x = metrics.x(shape.end_seconds);
    const juce::Rectangle<float> span{
        start_x,
        static_cast<float>(metrics.bounds.getY()),
        end_x - start_x,
        static_cast<float>(metrics.bounds.getHeight())
    };
    if (span.getWidth() <= 0.0f)
    {
        return;
    }

    const juce::Colour outline = juce::Colours::white.withAlpha(0.3f);
    if (shape.arpeggio)
    {
        constexpr std::array<float, 2> dashes{5.0f, 4.0f};
        g.setColour(outline);
        g.drawDashedLine(
            juce::Line<float>{span.getX(), span.getY(), span.getX(), span.getBottom()},
            dashes.data(),
            static_cast<int>(dashes.size()),
            1.0f);
        g.drawDashedLine(
            juce::Line<float>{span.getRight(), span.getY(), span.getRight(), span.getBottom()},
            dashes.data(),
            static_cast<int>(dashes.size()),
            1.0f);
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillRect(span);
        g.setColour(outline);
        g.drawRect(span, 1.0f);
    }

    if (metrics.draw_text && !shape.name.empty())
    {
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(metrics.badge_font);
        g.drawText(
            juce::String{shape.name},
            juce::Rectangle<float>{
                span.getX() + 3.0f,
                span.getY() + 1.0f,
                std::max(24.0f, span.getWidth()),
                metrics.badge_font.getHeight()
            },
            juce::Justification::topLeft);
    }
}

// Draws one fret-hand-position marker: a small boxed fret number along the lane's bottom edge.
void drawFhpMarker(juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabFhpView& fhp)
{
    if (!metrics.draw_text)
    {
        return;
    }

    const float marker_x = metrics.x(fhp.seconds);
    const juce::String text{fhp.fret};
    const float width = static_cast<float>(textWidth(metrics.badge_font, text)) + 6.0f;
    constexpr float height = 12.0f;
    const juce::Rectangle<float> box{
        marker_x, static_cast<float>(metrics.bounds.getBottom()) - height - 1.0f, width, height
    };
    g.setColour(juce::Colour{0xff2a2f36});
    g.fillRoundedRectangle(box, 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(metrics.badge_font);
    g.drawText(text, box, juce::Justification::centred);
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
juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    const int below_standard_window = std::max(0, displayed_string_count - 6);
    if (displayed_string > below_standard_window)
    {
        const auto standard_index =
            static_cast<std::size_t>(displayed_string - below_standard_window - 1);
        return juce::Colour{g_standard_string_colors.at(
            std::min(standard_index, g_standard_string_colors.size() - 1))};
    }

    const auto tertiary_index = static_cast<std::size_t>(below_standard_window - displayed_string);
    return juce::Colour{g_tertiary_string_colors.at(
        tertiary_index % g_tertiary_string_colors.size())};
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

// Draws the visible chart content in layers — shape spans behind, then notes with their
// techniques, then fret-hand-position markers — clipped to the repaint span.
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
    const TabLaneMetrics metrics{
        .bounds = bounds,
        .visible_timeline = m_visible_timeline,
        .displayed_count = displayed_count,
        .extra_lanes = displayed_count - m_tab->string_count,
        .lane_height = lane_height,
        .head_height = head_height,
        .sustain_height = std::max(2.0f, head_height / 3.0f),
        .draw_text = head_height >= static_cast<float>(g_min_head_height_for_text),
        .fret_font = juce::Font{juce::FontOptions{std::max(8.0f, head_height - 4.0f)}},
        .badge_font = juce::Font{juce::FontOptions{std::max(8.0f, head_height * 0.65f)}},
    };

    // Heads and badges extend a fixed pixel slack around their onset, so the visible span grows
    // by that slack before the visibility queries.
    const juce::Rectangle<int> clip = g.getClipBounds();
    const double seconds_per_pixel = duration / static_cast<double>(bounds.getWidth());
    const double slack_seconds = static_cast<double>(g_max_head_height) * 3.0 * seconds_per_pixel;
    const double span_start = m_visible_timeline.start.seconds +
                              static_cast<double>(clip.getX()) * seconds_per_pixel - slack_seconds;
    const double span_end = m_visible_timeline.start.seconds +
                            static_cast<double>(clip.getRight()) * seconds_per_pixel +
                            slack_seconds;

    // Shape spans tint the background behind the notes they cover.
    for (const core::TabShapeView& shape : m_tab->shapes)
    {
        if (shape.end_seconds >= span_start && shape.start_seconds <= span_end)
        {
            drawShapeSpan(g, metrics, shape);
        }
    }

    const auto [first, last] =
        tabVisibleNoteRange(m_tab->notes, m_prefix_max_end_seconds, span_start, span_end);
    for (std::size_t index = first; index < last; ++index)
    {
        const core::TabNoteView& note = m_tab->notes[index];
        if (note.end_seconds < span_start)
        {
            continue;
        }

        const float center_y = metrics.laneY(note.string);
        const float onset_x = metrics.x(note.start_seconds);
        const juce::Colour lane_color = metrics.laneColor(note.string);

        drawSustainTail(g, metrics, note, onset_x, center_y, lane_color);
        drawSlideWaypoints(g, metrics, note, onset_x, center_y, lane_color);
        drawBendCurve(g, metrics, note, onset_x, center_y);
        drawNoteHead(g, metrics, note, onset_x, center_y, lane_color);
        drawTechniqueBadges(g, metrics, note, onset_x, center_y);
    }

    for (const core::TabFhpView& fhp : m_tab->fret_hand_positions)
    {
        if (fhp.seconds >= span_start && fhp.seconds <= span_end)
        {
            drawFhpMarker(g, metrics, fhp);
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
