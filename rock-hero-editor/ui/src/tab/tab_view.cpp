#include "tab/tab_view.h"

#include "shared/text_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// The string-color palette and its Charter-exact derivation chain live in rock-hero-common/ui
// (shared with the game highway and the future editor 3D preview); this renderer consumes the
// Charter Classic preset and converts to JUCE colors at this module's boundary.

// Charter modern-theme fixed colors.
const juce::Colour g_note_background_color{0xff101010};     // NOTE_BACKGROUND
const juce::Colour g_hand_shape_color{0xff3157a7};          // HAND_SHAPE
const juce::Colour g_hand_shape_arpeggio_color{0xff8559b7}; // HAND_SHAPE_ARPEGGIO
const juce::Colour g_vibrato_sine_color{0xffb6b6b6};        // java Color.GRAY.brighter()
const juce::Colour g_mute_border_color{0xff808080};         // java Color.GRAY
const juce::Colour g_full_mute_text_border{0xffc0c0c0};     // java Color.LIGHT_GRAY

// Charter's default noteHeight; lanes big enough to fit it render at exactly Charter's scale.
constexpr float g_charter_note_height{25.0f};

// Notes smaller than this cannot fit readable fret numbers and drop to bare markers.
constexpr float g_min_note_height_for_text{9.0f};

// Height of the hand-shape label bar and its bold name text (Charter chartTextHeight).
constexpr float g_shape_label_height{10.0f};
constexpr float g_shape_rail_height{3.0f};
constexpr double g_shape_mark_brightness{1.5};
constexpr double g_ghost_note_darkness{0.35};

// Thin JUCE-converting wrappers over the shared Charter-exact derivation (rock-hero-common/ui)
// for the in-file call sites that derive from already-opaque colors.
[[nodiscard]] juce::Colour charterDarker(juce::Colour color)
{
    return juce::Colour{common::ui::darkerColor(color.getARGB())};
}

[[nodiscard]] juce::Colour charterMultiply(juce::Colour color, double multiplier)
{
    return juce::Colour{common::ui::multiplyColor(color.getARGB(), multiplier)};
}

// Hand-shape mark color: the Charter base brightened so the narrow rails, onset bars, and name
// chips read clearly against the dark lane (user-directed brightness bump).
[[nodiscard]] juce::Colour shapeMarkColor(bool arpeggio)
{
    return charterMultiply(
        arpeggio ? g_hand_shape_arpeggio_color : g_hand_shape_color, g_shape_mark_brightness);
}

// Bridges the shared Charter-exact style derivation (rock-hero-common/ui) to JUCE colors at
// this module's boundary; field meanings match common::ui::StringLaneStyle one for one.
struct StringStyle
{
    juce::Colour lane;         // string line: base x0.8
    juce::Colour border_inner; // note ring: lane brightened
    juce::Colour inner;        // note fill: ring darkened twice
    juce::Colour linked_inner; // linked-note fill: the fill darkened twice more
    juce::Colour tail;         // sustain fill: base x0.66
    juce::Colour tail_edge;    // sustain border: tail brightened
    juce::Colour accent;       // accent glow: ring brightened twice

    explicit StringStyle(juce::Colour base)
        : StringStyle(common::ui::StringLaneStyle{base.getARGB()})
    {}

    explicit StringStyle(const common::ui::StringLaneStyle& style)
        : lane(style.lane)
        , border_inner(style.border_inner)
        , inner(style.inner)
        , linked_inner(style.linked_inner)
        , tail(style.tail)
        , tail_edge(style.tail_edge)
        , accent(style.accent)
    {}
};

// A floating label chip collected during the tail passes and drawn above every note head.
struct LabelChip
{
    juce::Point<float> position;
    juce::String text;
    juce::Colour background;
    juce::Colour border;
};

// Maps a timeline time onto the component's horizontal axis.
[[nodiscard]] float xForSeconds(
    double seconds, common::core::TimeRange visible_timeline, int width) noexcept
{
    const double duration = visible_timeline.duration().seconds;
    return static_cast<float>(
        (seconds - visible_timeline.start.seconds) / duration * static_cast<double>(width));
}

// Layout facts shared by every glyph pass of one paint call. Sizes follow Charter's DrawerUtils:
// the lane height fixes the note height (laneHeight = 1.5 x noteHeight) and everything else
// derives from the note height with Charter's ratios.
struct TabLaneMetrics
{
    juce::Rectangle<int> bounds;
    common::core::TimeRange visible_timeline;
    int displayed_count{};
    int extra_lanes{};
    float lane_height{};
    float note_height{};
    float tail_height{};
    float tail_edge_size{};
    float tremolo_size{};
    bool draw_text{};
    // Initialized via FontOptions because JUCE 8 deprecates the default Font constructor; the
    // placeholder values are replaced by makeMetrics before any drawing.
    juce::Font fret_font{juce::FontOptions{}};
    juce::Font bend_font{juce::FontOptions{}};
    juce::Font label_font{juce::FontOptions{}};

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

    // Base color for a chart string, accounting for extra user lanes below the chart.
    [[nodiscard]] juce::Colour baseColor(int chart_string) const
    {
        return tabStringColor(chart_string + extra_lanes, displayed_count);
    }
};

[[nodiscard]] TabLaneMetrics makeMetrics(
    juce::Rectangle<int> bounds, common::core::TimeRange visible_timeline, int displayed_count,
    int chart_string_count)
{
    TabLaneMetrics metrics;
    metrics.bounds = bounds;
    metrics.visible_timeline = visible_timeline;
    metrics.displayed_count = displayed_count;
    metrics.extra_lanes = displayed_count - chart_string_count;
    // Lanes evenly fill the row, which the host sizes proportionally to the count; this matches
    // tabLaneCenterY, so note height stays at the reference-density value whatever the count.
    metrics.lane_height =
        static_cast<float>(bounds.getHeight()) / static_cast<float>(displayed_count);
    metrics.note_height = std::min(g_charter_note_height, metrics.lane_height / 1.5f);
    // Charter keeps the tail height odd so the tail centers on the string line.
    const auto odd = [](float value) {
        const int rounded = static_cast<int>(value);
        return static_cast<float>(rounded % 2 == 0 ? rounded + 1 : rounded);
    };
    metrics.tail_height = odd(metrics.note_height * 3.0f / 4.0f);
    metrics.tail_edge_size = std::max(1.0f, metrics.tail_height / 8.0f);
    metrics.tremolo_size = std::max(2.0f, metrics.tail_height / 6.0f);
    metrics.draw_text = metrics.note_height >= g_min_note_height_for_text;
    metrics.fret_font =
        juce::Font{juce::FontOptions{std::max(8.0f, metrics.note_height / 2.0f)}.withStyle("Bold")};
    metrics.bend_font = juce::Font{juce::FontOptions{std::max(10.0f, metrics.note_height / 4.0f)}};
    metrics.label_font = juce::Font{juce::FontOptions{g_shape_label_height}.withStyle("Bold")};
    return metrics;
}

// Draws one string line per displayed lane across the visible clip, exactly like Charter's lane
// lines: one pixel, the string's base color at 80%.
void drawStringLines(juce::Graphics& g, const TabLaneMetrics& metrics)
{
    const juce::Rectangle<int> clip = g.getClipBounds();
    for (int displayed_string = 1; displayed_string <= metrics.displayed_count; ++displayed_string)
    {
        const float y = tabLaneCenterY(displayed_string, metrics.displayed_count, metrics.bounds);
        g.setColour(
            charterMultiply(tabStringColor(displayed_string, metrics.displayed_count), 0.8));
        // Snapped to a whole pixel row so the one-pixel line stays crisp like Charter's.
        g.fillRect(clip.getX(), static_cast<int>(y), clip.getWidth(), 1);
    }
}

// Charter formats bend amounts in whole steps with quarter fractions ("0", "½", "1 ¼", ...).
[[nodiscard]] juce::String charterBendText(double semitones)
{
    const auto quarter_steps = static_cast<int>(std::lround(semitones * 2.0));
    const int full_steps = quarter_steps / 4;
    const int quarters = quarter_steps % 4;
    constexpr std::array<const char*, 4> fragments{"", "\xC2\xBC", "\xC2\xBD", "\xC2\xBE"};
    const juce::String fragment{juce::CharPointer_UTF8{fragments.at(
        static_cast<std::size_t>(std::max(0, quarters)))}};

    if (full_steps == 0)
    {
        return quarters == 0 ? juce::String{"0"} : fragment;
    }

    juce::String text{full_steps};
    if (quarters != 0)
    {
        text += " " + fragment;
    }
    return text;
}

// Vertical span of a sustain tail around the string line (Charter's default tail top/bottom).
struct TailSpan
{
    float top;
    float bottom;
};

[[nodiscard]] TailSpan tailSpan(const TabLaneMetrics& metrics, float center_y)
{
    return TailSpan{
        .top = center_y - metrics.tail_height / 3.0f,
        .bottom = center_y + metrics.tail_height / 3.0f + 1.0f,
    };
}

// Draws the tremolo tail as Charter's repeating pointed-gem fragments: an edge-colored zigzag
// with the tail color inset vertically by the edge size. Charter's final partial fragment
// distorts the last gem; here the full pattern is clipped to the tail span instead, which keeps
// every tooth shaped correctly and ends the strip on a clean vertical edge (the one deliberate
// bug fix in an otherwise faithful port).
void drawTremoloTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style, float x,
    float length, float center_y)
{
    const float fragment_size = std::max(4.0f, metrics.note_height / 2.0f);
    const float y0 = center_y + metrics.tail_height / 2.0f - metrics.tremolo_size + 1.0f;
    const float y1 = center_y + metrics.tail_height / 2.0f + 1.0f;
    const float y2 = center_y - metrics.tail_height / 2.0f + metrics.tremolo_size;
    const float y3 = center_y - metrics.tail_height / 2.0f;

    const auto add_fragments = [&](juce::Path& path, float inset) {
        const auto fragment_count = static_cast<int>(length / fragment_size) + 1;
        for (int fragment = 0; fragment < fragment_count; ++fragment)
        {
            const float fragment_x = x + static_cast<float>(fragment) * fragment_size;
            juce::Path gem;
            gem.startNewSubPath(fragment_x, y0 - inset);
            gem.lineTo(fragment_x + fragment_size / 2.0f, y1 - inset);
            gem.lineTo(fragment_x + fragment_size, y0 - inset);
            gem.lineTo(fragment_x + fragment_size, y2 + inset);
            gem.lineTo(fragment_x + fragment_size / 2.0f, y3 + inset);
            gem.lineTo(fragment_x, y2 + inset);
            gem.closeSubPath();
            path.addPath(gem);
        }
    };

    g.saveState();
    g.reduceClipRegion(
        juce::Rectangle<float>{x, y3, length, y1 - y3}.getSmallestIntegerContainer());
    juce::Path edge_fragments;
    add_fragments(edge_fragments, 0.0f);
    g.setColour(style.tail_edge);
    g.fillPath(edge_fragments);

    juce::Path inner_fragments;
    add_fragments(inner_fragments, metrics.tail_edge_size);
    g.setColour(style.tail);
    g.fillPath(inner_fragments);
    g.restoreState();
}

// Draws the sustain tail: Charter's filled bar with a brighter stroked border, the tremolo gem
// strip variant, and the vibrato sine overlay.
void drawNoteTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const core::TabNoteView& note, float onset_x, float center_y)
{
    const float end_x = metrics.x(note.end_seconds);
    const float length = end_x - onset_x;
    if (length <= 0.0f)
    {
        return;
    }

    const TailSpan span = tailSpan(metrics, center_y);
    if (note.tremolo)
    {
        drawTremoloTail(g, metrics, style, onset_x, length, center_y);
    }
    else
    {
        g.setColour(style.tail);
        g.fillRect(
            juce::Rectangle<float>{
                onset_x - 1.0f, span.top, length + 1.0f, span.bottom - span.top
            });
        g.setColour(style.tail_edge);
        g.drawRect(
            juce::Rectangle<float>{onset_x, span.top - 1.0f, length, span.bottom - span.top + 1.0f},
            metrics.tail_edge_size);
    }

    if (note.vibrato)
    {
        const float amplitude = metrics.tail_height / 3.0f;
        const float period = amplitude * 3.0f;
        juce::Path wave;
        wave.startNewSubPath(onset_x, center_y);
        const auto wave_pixels = static_cast<int>(length);
        for (int step = 1; step <= wave_pixels; ++step)
        {
            const auto dx = static_cast<float>(step);
            wave.lineTo(
                onset_x + dx,
                center_y + amplitude * std::sin(dx * juce::MathConstants<float>::twoPi / period));
        }
        g.setColour(g_vibrato_sine_color);
        g.strokePath(wave, juce::PathStrokeType{std::max(1.0f, metrics.tail_height / 8.0f)});
    }
}

// Fills Charter's layered note-head shape: a dark outer ring, a bright string-colored ring, and
// a colored center (dimmed for normal heads, doubly dimmed for linked heads). Harmonic notes use
// the diamond silhouette of the same layers.
void fillHeadShape(
    juce::Graphics& g, juce::Colour border_inner, juce::Colour inner, float center_x,
    float center_y, float size, bool diamond)
{
    const float border = std::max(1.0f, size / 15.0f);

    const auto layer = [&](float inset, juce::Colour color) {
        const float extent = size - 2.0f * inset;
        g.setColour(color);
        if (diamond)
        {
            juce::Path shape;
            shape.startNewSubPath(center_x, center_y - extent / 2.0f);
            shape.lineTo(center_x + extent / 2.0f, center_y);
            shape.lineTo(center_x, center_y + extent / 2.0f);
            shape.lineTo(center_x - extent / 2.0f, center_y);
            shape.closeSubPath();
            g.fillPath(shape);
        }
        else
        {
            g.fillEllipse(center_x - extent / 2.0f, center_y - extent / 2.0f, extent, extent);
        }
    };

    layer(0.0f, g_note_background_color);
    layer(border, border_inner);
    layer(border * 2.0f, inner);
}

// Draws Charter's slide line: a white two-pixel diagonal across the tail toward the target fret,
// rising for ascending slides. Waypoint chains continue segment by segment; unpitched targets
// get Charter's fret label chip (white on the tail color darkened three times) at the segment
// end, exactly as Charter labels unpitched slides.
void drawSlideLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const core::TabNoteView& note, float onset_x, float center_y,
    std::vector<LabelChip>& slide_labels)
{
    if (note.slides.empty())
    {
        return;
    }

    constexpr float line_thickness = 2.0f;
    const TailSpan span = tailSpan(metrics, center_y);
    float from_x = onset_x + metrics.note_height / 4.0f;
    int previous_fret = note.fret;
    for (const core::TabSlideView& waypoint : note.slides)
    {
        const bool upward = waypoint.fret >= previous_fret;
        const float from_y =
            upward ? span.bottom - line_thickness / 2.0f : span.top + line_thickness / 2.0f;
        const float to_y =
            upward ? span.top + line_thickness / 2.0f : span.bottom - line_thickness / 2.0f;
        const float to_x = metrics.x(waypoint.seconds) - line_thickness;

        g.setColour(juce::Colours::white);
        g.drawLine(from_x, from_y, to_x, to_y, line_thickness);

        if (waypoint.unpitched && metrics.draw_text)
        {
            const float label_y = upward ? span.top - metrics.note_height / 3.0f
                                         : span.bottom + metrics.note_height / 3.0f;
            slide_labels.push_back(
                LabelChip{
                    .position = {metrics.x(waypoint.seconds), label_y},
                    .text = juce::String{waypoint.fret},
                    .background = charterDarker(charterDarker(charterDarker(style.tail))),
                    .border = style.tail,
                });
        }

        from_x = to_x;
        previous_fret = waypoint.fret;
    }
}

// Draws Charter's linked-note head (the same layered circle with a doubly darkened center) with
// its fret number at each pitched slide waypoint. Charter charts express slide chains as linked
// notes and draws one of these at every link; our format merges the chain into waypoints, so the
// waypoints are exactly where Charter's linked heads sit.
void drawSlideWaypointHeads(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const core::TabNoteView& note, float center_y)
{
    for (const core::TabSlideView& waypoint : note.slides)
    {
        if (waypoint.unpitched)
        {
            continue;
        }

        const float x = metrics.x(waypoint.seconds);
        const float size = metrics.note_height + 1.0f;
        fillHeadShape(g, style.border_inner, style.linked_inner, x, center_y, size, false);
        if (metrics.draw_text)
        {
            g.setColour(juce::Colours::white);
            g.setFont(metrics.fret_font);
            g.drawText(
                juce::String{waypoint.fret},
                juce::Rectangle<float>{x - size, center_y - size, size * 2.0f, size * 2.0f},
                juce::Justification::centred);
        }
    }
}

// Draws Charter's bend presentation: a white two-pixel polyline stepping between bend heights
// over the tail, then a flat run to the tail end, with a "ノ<amount>" chip at each bend point
// (white text on the string's lane color darkened twice).
void drawBendLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const core::TabNoteView& note, float onset_x, float center_y,
    std::vector<LabelChip>& bend_chips)
{
    if (note.bend.empty())
    {
        return;
    }

    // Charter maps bend height across two thirds of the tail, full at three whole steps.
    const auto bend_y = [&](double semitones) {
        const double steps = std::clamp(semitones / 2.0, 0.0, 3.0);
        return center_y + metrics.tail_height / 3.0f -
               static_cast<float>(steps / 3.0) * metrics.tail_height * 2.0f / 3.0f;
    };

    const juce::Colour chip_background = charterDarker(charterDarker(style.lane));
    const float end_x = metrics.x(note.end_seconds);
    juce::Point<float> last{onset_x, bend_y(0.0)};
    g.setColour(juce::Colours::white);
    for (const core::TabBendPointView& point : note.bend)
    {
        const juce::Point<float> to{metrics.x(point.seconds), bend_y(point.semitones)};
        g.drawLine(last.x, last.y, to.x, to.y, 2.0f);
        if (metrics.draw_text)
        {
            // Chips sit on the bend line, or above the head when the bend is at the onset.
            const bool over_head = to.x <= onset_x + metrics.note_height / 2.0f;
            const float chip_y = over_head ? center_y - metrics.note_height / 2.0f -
                                                 metrics.bend_font.getHeight() / 2.0f - 1.0f
                                           : to.y - metrics.tail_height / 2.0f;
            bend_chips.push_back(
                LabelChip{
                    .position = {to.x, chip_y},
                    .text = juce::String{juce::CharPointer_UTF8{"\xE3\x83\x8E"}} +
                            charterBendText(point.semitones),
                    .background = chip_background,
                    .border = chip_background,
                });
        }
        last = {to.x + 1.0f, to.y};
    }
    g.drawLine(last.x, last.y, end_x - 2.0f, last.y, 2.0f);
}

// Draws Charter's accent glow behind the head: a soft ring fading out just past the head edge.
void drawAccentGlow(
    juce::Graphics& g, const StringStyle& style, float center_x, float center_y, float size,
    bool diamond)
{
    const float glow_size = size * 1.4f;
    if (diamond)
    {
        // Concentric fading diamond outlines approximate Charter's diamond-distance fade.
        for (int ring = 0; ring < 4; ++ring)
        {
            const float extent = glow_size * (0.8f + 0.05f * static_cast<float>(ring));
            juce::Path shape;
            shape.startNewSubPath(center_x, center_y - extent / 2.0f);
            shape.lineTo(center_x + extent / 2.0f, center_y);
            shape.lineTo(center_x, center_y + extent / 2.0f);
            shape.lineTo(center_x - extent / 2.0f, center_y);
            shape.closeSubPath();
            g.setColour(style.accent.withAlpha(1.0f - 0.25f * static_cast<float>(ring)));
            g.strokePath(shape, juce::PathStrokeType{glow_size * 0.05f});
        }
        return;
    }

    juce::ColourGradient gradient{
        style.accent,
        center_x,
        center_y,
        style.accent.withAlpha(0.0f),
        center_x,
        center_y + glow_size / 2.0f,
        true
    };
    gradient.addColour(0.8, style.accent);
    gradient.addColour(0.95, style.accent.withAlpha(0.0f));
    g.setGradientFill(gradient);
    g.fillEllipse(center_x - glow_size / 2.0f, center_y - glow_size / 2.0f, glow_size, glow_size);
}

// Draws Charter's fat X mute icon over the head: near-black for palm mutes, white for full
// mutes, both with a gray border.
void drawMuteIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, common::core::NoteMute mute, float center_x,
    float center_y)
{
    if (mute == common::core::NoteMute::None)
    {
        return;
    }

    const float size = std::max(16.0f, metrics.note_height + 1.0f);
    const float space = std::max(2.0f, size / 8.0f);
    const float half = size / 2.0f;
    const float left = center_x - half;
    const float top = center_y - half;

    juce::Path x_shape;
    x_shape.startNewSubPath(left, top + space);
    x_shape.lineTo(left + half - space, top + half);
    x_shape.lineTo(left, top + size - space);
    x_shape.lineTo(left + space, top + size);
    x_shape.lineTo(left + half, top + half + space);
    x_shape.lineTo(left + size - space, top + size);
    x_shape.lineTo(left + size, top + size - space);
    x_shape.lineTo(left + half + space, top + half);
    x_shape.lineTo(left + size, top + space);
    x_shape.lineTo(left + size - space, top);
    x_shape.lineTo(left + half, top + half - space);
    x_shape.lineTo(left + space, top);
    x_shape.closeSubPath();

    const juce::Colour inner =
        mute == common::core::NoteMute::Full ? juce::Colours::white : juce::Colour{0xff050505};
    g.setColour(inner);
    g.fillPath(x_shape);
    g.setColour(g_mute_border_color);
    g.strokePath(x_shape, juce::PathStrokeType{std::max(1.0f, space / 3.0f)});
}

// Draws one of Charter's triangle technique icons: a small bordered triangle beside the head,
// optionally carrying a letter (slap and pop).
void drawTriangleIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, float center_x, float center_y,
    bool pointing_down, juce::Colour fill, juce::Colour border, const juce::String& letter)
{
    const float width = metrics.note_height / 2.0f;
    const float height = metrics.note_height * 2.0f / 5.0f;
    const float left = center_x - width / 2.0f;
    const float top = center_y - height / 2.0f;

    juce::Path triangle;
    if (pointing_down)
    {
        triangle.addTriangle(left, top, left + width, top, left + width / 2.0f, top + height);
    }
    else
    {
        triangle.addTriangle(
            left, top + height, left + width / 2.0f, top, left + width, top + height);
    }
    g.setColour(fill);
    g.fillPath(triangle);
    g.setColour(border);
    g.strokePath(triangle, juce::PathStrokeType{1.0f});

    if (letter.isNotEmpty())
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font{juce::FontOptions{std::max(6.0f, height * 0.9f)}.withStyle("Bold")});
        g.drawText(
            letter,
            juce::Rectangle<float>{left, top - (pointing_down ? 1.0f : -1.0f), width, height},
            juce::Justification::centred);
    }
}

// Draws the attack technique icon in Charter's position and style: hammer-ons, pull-offs, and
// taps sit left of the head; slaps and pops sit right of it.
void drawAttackIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabNoteView& note, float center_x,
    float center_y)
{
    const float left_x = center_x - metrics.note_height / 2.0f;
    const float right_x = center_x + metrics.note_height / 2.0f;
    const float high_y = center_y - metrics.note_height / 2.0f;
    const float low_y = center_y - metrics.note_height / 3.0f;

    switch (note.attack)
    {
        case common::core::NoteAttack::Hammer:
        {
            drawTriangleIcon(
                g, metrics, left_x, low_y, true, juce::Colours::white, juce::Colours::black, {});
            break;
        }
        case common::core::NoteAttack::Pull:
        {
            drawTriangleIcon(
                g, metrics, left_x, high_y, false, juce::Colours::white, juce::Colours::black, {});
            break;
        }
        case common::core::NoteAttack::Tap:
        {
            drawTriangleIcon(
                g, metrics, left_x, low_y, true, juce::Colours::black, g_full_mute_text_border, {});
            break;
        }
        case common::core::NoteAttack::Slap:
        {
            drawTriangleIcon(
                g,
                metrics,
                right_x,
                low_y,
                true,
                juce::Colours::black,
                g_full_mute_text_border,
                "S");
            break;
        }
        case common::core::NoteAttack::Pop:
        {
            drawTriangleIcon(
                g,
                metrics,
                right_x,
                low_y,
                false,
                juce::Colours::black,
                g_full_mute_text_border,
                "P");
            break;
        }
        case common::core::NoteAttack::Pick:
        {
            break;
        }
    }
}

// Draws the complete note head stack in Charter's order: accent glow, layered head shape, the
// pinch-harmonic edge line, mute icon, fret number, then the attack icon.
void drawNoteHead(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const core::TabNoteView& note, float onset_x, float center_y)
{
    // Charter renders heads one pixel larger than the configured height so they get a center
    // pixel on the string line.
    const float size = metrics.note_height + 1.0f;
    const bool diamond = note.harmonic != common::core::NoteHarmonic::None;

    if (note.accent)
    {
        drawAccentGlow(g, style, onset_x, center_y, size, diamond);
    }

    fillHeadShape(g, style.border_inner, style.inner, onset_x, center_y, size, diamond);

    if (note.harmonic == common::core::NoteHarmonic::Pinch)
    {
        const float line_x = onset_x - metrics.note_height / 2.0f;
        g.setColour(style.border_inner);
        g.fillRect(
            juce::Rectangle<float>{
                line_x - 1.5f, center_y - metrics.note_height / 2.0f, 3.0f, metrics.note_height
            });
    }

    drawMuteIcon(g, metrics, note.mute, onset_x, center_y);

    if (metrics.draw_text)
    {
        const juce::String fret_text{note.fret};
        if (note.mute == common::core::NoteMute::Full)
        {
            // Charter boxes the fret number on full mutes so it stays readable over the X.
            const auto text_width = static_cast<float>(textWidth(metrics.fret_font, fret_text));
            const juce::Rectangle<float> box{
                onset_x - text_width / 2.0f - 2.0f,
                center_y - metrics.fret_font.getHeight() / 2.0f - 1.0f,
                text_width + 4.0f,
                metrics.fret_font.getHeight() + 2.0f
            };
            g.setColour(g_mute_border_color);
            g.fillRect(box);
            g.setColour(g_full_mute_text_border);
            g.drawRect(box, 1.0f);
        }
        g.setColour(juce::Colours::white);
        g.setFont(metrics.fret_font);
        g.drawText(
            fret_text,
            juce::Rectangle<float>{onset_x - size, center_y - size, size * 2.0f, size * 2.0f},
            juce::Justification::centred);
    }

    drawAttackIcon(g, metrics, note, onset_x, center_y);
}

// Draws one hand-shape span as narrow rails along the lane's top and bottom edges for the
// span's duration — blue for chord shapes, purple for arpeggios — echoing the 3D highway's
// shape rails at the hand-window fret lines (a user-directed departure from Charter's
// full-height tint, which read as an ugly wall of color). The template name, when present,
// rides a small chip against the bottom rail at the span start: the bottom edge is where the
// name has always lived, and the top edge belongs to the FHP markers.
void drawShapeSpan(
    juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabShapeView& shape)
{
    const float start_x = metrics.x(shape.start_seconds);
    const float end_x = metrics.x(shape.end_seconds);
    if (end_x <= start_x)
    {
        return;
    }

    const juce::Colour color = shapeMarkColor(shape.arpeggio);
    const float width = end_x - start_x;
    const float bottom_rail_y =
        static_cast<float>(metrics.bounds.getBottom()) - g_shape_rail_height;
    g.setColour(color);
    g.fillRect(
        juce::Rectangle<float>{
            start_x, static_cast<float>(metrics.bounds.getY()), width, g_shape_rail_height
        });
    g.fillRect(juce::Rectangle<float>{start_x, bottom_rail_y, width, g_shape_rail_height});

    if (metrics.draw_text && !shape.name.empty())
    {
        const juce::String name{shape.name};
        const float chip_width = static_cast<float>(textWidth(metrics.label_font, name)) + 6.0f;
        const float chip_height = g_shape_label_height + 2.0f;
        const juce::Rectangle<float> chip{
            start_x, bottom_rail_y - chip_height, chip_width, chip_height
        };
        g.setColour(color);
        g.fillRoundedRectangle(chip, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(metrics.label_font);
        g.drawText(name, chip, juce::Justification::centred);
    }
}

// Draws a narrow vertical bar spanning the lane height at an onset, rail-width and solid so it
// reads as one family with the horizontal span rails (a user-directed restyle of Charter's
// wider translucent teal chord box): blue at strummed chord onsets, purple at arpeggio starts.
void drawOnsetBar(juce::Graphics& g, const TabLaneMetrics& metrics, float x, juce::Colour color)
{
    g.setColour(color);
    g.fillRect(
        juce::Rectangle<float>{
            x - g_shape_rail_height / 2.0f,
            static_cast<float>(metrics.bounds.getY()),
            g_shape_rail_height,
            static_cast<float>(metrics.bounds.getHeight())
        });
}

// Draws one fret-hand-position marker: a small boxed fret number along the lane's top edge.
// This presentation is ours, not Charter's (Charter shows FHPs in a separate strip above the
// lanes, which this single-row lane does not have); it stays deliberately unobtrusive until the
// FHP display treatment is decided.
void drawFhpMarker(juce::Graphics& g, const TabLaneMetrics& metrics, const core::TabFhpView& fhp)
{
    if (!metrics.draw_text)
    {
        return;
    }

    const float marker_x = metrics.x(fhp.seconds);
    const juce::String text{fhp.fret};
    const float width = static_cast<float>(textWidth(metrics.label_font, text)) + 6.0f;
    constexpr float height = 12.0f;
    const juce::Rectangle<float> box{
        marker_x, static_cast<float>(metrics.bounds.getY()) + 1.0f, width, height
    };
    g.setColour(juce::Colour{0xff2a2f36});
    g.fillRoundedRectangle(box, 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(metrics.label_font);
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

// Thin wrapper over the shared palette so the editor/ui surface and its tests stay unchanged;
// the lane-window logic lives with the palette in rock-hero-common/ui.
juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return juce::Colour{common::ui::stringLaneColor(
        displayed_string, displayed_string_count, common::ui::charterClassicPalette())};
}

// Standard tablature orientation: highest string on top, lowest on the bottom. The host sizes
// the row proportionally to the string count (TrackViewport at the six-string reference density),
// so evenly dividing the row's height yields identical per-lane spacing at every count and the
// waveform behind the lanes hugs them with no empty margin.
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

// Draws the visible chart content in Charter's layer order: string lines, hand-shape spans,
// sustain tails with their slide and bend lines, chord and arpeggio onset pills, note heads
// with technique glyphs, then the floating labels (slide frets and bend amount chips) on top.
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
    const TabLaneMetrics metrics =
        makeMetrics(bounds, m_visible_timeline, displayed_count, m_tab->string_count);

    // Heads and icons extend a fixed pixel slack around their onset, so the visible span grows
    // by that slack before the visibility queries.
    const juce::Rectangle<int> clip = g.getClipBounds();
    const double seconds_per_pixel = duration / static_cast<double>(bounds.getWidth());
    const double slack_seconds =
        static_cast<double>(g_charter_note_height) * 3.0 * seconds_per_pixel;
    const double span_start = m_visible_timeline.start.seconds +
                              static_cast<double>(clip.getX()) * seconds_per_pixel - slack_seconds;
    const double span_end = m_visible_timeline.start.seconds +
                            static_cast<double>(clip.getRight()) * seconds_per_pixel +
                            slack_seconds;

    drawStringLines(g, metrics);

    for (const core::TabShapeView& shape : m_tab->shapes)
    {
        if (shape.end_seconds >= span_start && shape.start_seconds <= span_end)
        {
            drawShapeSpan(g, metrics, shape);
        }
    }

    const auto [first, last] =
        tabVisibleNoteRange(m_tab->notes, m_prefix_max_end_seconds, span_start, span_end);

    // Floating labels collected during the note passes and drawn above every head.
    std::vector<LabelChip> slide_labels;
    std::vector<LabelChip> bend_chips;

    // Tails first so heads always cover their own tail starts (Charter's noteTails layer).
    for (std::size_t index = first; index < last; ++index)
    {
        const core::TabNoteView& note = m_tab->notes[index];
        if (note.end_seconds < span_start)
        {
            continue;
        }

        const StringStyle style{metrics.baseColor(note.string)};
        const float center_y = metrics.laneY(note.string);
        const float onset_x = metrics.x(note.start_seconds);
        drawNoteTail(g, metrics, style, note, onset_x, center_y);
        drawSlideLines(g, metrics, style, note, onset_x, center_y, slide_labels);
        drawBendLines(g, metrics, style, note, onset_x, center_y, bend_chips);
    }

    // Onset bars at strummed chord onsets: two or more notes sharing one start (Charter draws
    // these under the heads).
    for (std::size_t index = first; index < last; ++index)
    {
        const core::TabNoteView& note = m_tab->notes[index];
        // Exact equality via is_eq/is_neq keeps -Wfloat-equal builds clean: same-onset notes
        // carry bit-identical seconds because the projection resolves one grid position once.
        const bool starts_group =
            index + 1 < m_tab->notes.size() &&
            std::is_eq(m_tab->notes[index + 1].start_seconds <=> note.start_seconds) &&
            (index == 0 ||
             std::is_neq(m_tab->notes[index - 1].start_seconds <=> note.start_seconds));
        if (starts_group)
        {
            drawOnsetBar(g, metrics, metrics.x(note.start_seconds), shapeMarkColor(false));
        }
    }

    // Arpeggio spans mark their start with the same bar in purple (their notes arrive
    // sequentially, so the strummed-group rule above never marks them), plus ghost heads for
    // the held posture's strings that are not struck right at the bracket start.
    for (const core::TabShapeView& shape : m_tab->shapes)
    {
        if (!shape.arpeggio || shape.start_seconds < span_start || shape.start_seconds > span_end)
        {
            continue;
        }

        const float start_x = metrics.x(shape.start_seconds);
        drawOnsetBar(g, metrics, start_x, shapeMarkColor(true));
        for (const core::TabGhostNoteView& ghost : shape.ghost_notes)
        {
            // A ghost head is opaque but pulled hard toward the background — dark enough to
            // read as "held, not sounded here" while still covering the string line (earlier
            // translucent ghosts let the line cut through the head, which read badly) — and
            // its fret number stays at full strength so the posture remains readable.
            const StringStyle style{metrics.baseColor(ghost.string)};
            const float center_y = metrics.laneY(ghost.string);
            const float size = metrics.note_height + 1.0f;
            fillHeadShape(
                g,
                charterMultiply(style.border_inner, g_ghost_note_darkness),
                charterMultiply(style.inner, g_ghost_note_darkness),
                start_x,
                center_y,
                size,
                false);

            if (metrics.draw_text)
            {
                // The same centering box drawNoteHead uses for a plain fret number.
                g.setColour(juce::Colours::white);
                g.setFont(metrics.fret_font);
                g.drawText(
                    juce::String{ghost.fret},
                    juce::Rectangle<float>{
                        start_x - size, center_y - size, size * 2.0f, size * 2.0f
                    },
                    juce::Justification::centred);
            }
        }
    }

    for (std::size_t index = first; index < last; ++index)
    {
        const core::TabNoteView& note = m_tab->notes[index];
        if (note.end_seconds < span_start)
        {
            continue;
        }

        const StringStyle style{metrics.baseColor(note.string)};
        const float center_y = metrics.laneY(note.string);
        drawSlideWaypointHeads(g, metrics, style, note, center_y);
        drawNoteHead(g, metrics, style, note, metrics.x(note.start_seconds), center_y);
    }

    // Floating label chips draw over every head, like Charter's slideFrets and bendValues
    // layers: white text on the per-string chip color collected during the tail pass.
    const auto draw_chips =
        [&](const std::vector<LabelChip>& chips, const juce::Font& font, float pad) {
            g.setFont(font);
            for (const LabelChip& chip : chips)
            {
                const auto text_width = static_cast<float>(textWidth(font, chip.text));
                const juce::Rectangle<float> box{
                    chip.position.x - text_width / 2.0f - pad,
                    chip.position.y - font.getHeight() / 2.0f - 1.0f,
                    text_width + pad * 2.0f,
                    font.getHeight() + 2.0f
                };
                g.setColour(chip.background);
                g.fillRect(box);
                if (chip.border != chip.background)
                {
                    g.setColour(chip.border);
                    g.drawRect(box, 1.0f);
                }
                g.setColour(juce::Colours::white);
                g.drawText(chip.text, box, juce::Justification::centred);
            }
        };
    if (metrics.draw_text)
    {
        draw_chips(slide_labels, metrics.fret_font, 3.0f);
        draw_chips(bend_chips, metrics.bend_font, 2.0f);
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
