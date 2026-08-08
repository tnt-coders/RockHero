#include "tab/tab_paint_core.h"

#include "string_colors/string_color_palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// The string-color palette and its Charter-exact derivation chain live beside this core in
// rock-hero-common/ui; the drawers consume the Charter Classic preset and convert to JUCE
// colors at this module's boundary.

// Charter modern-theme fixed colors.
const juce::Colour g_note_background_color{0xff101010};     // NOTE_BACKGROUND
const juce::Colour g_hand_shape_color{0xff3157a7};          // HAND_SHAPE
const juce::Colour g_hand_shape_arpeggio_color{0xff8559b7}; // HAND_SHAPE_ARPEGGIO
const juce::Colour g_vibrato_sine_color{0xffb6b6b6};        // java Color.GRAY.brighter()
const juce::Colour g_mute_border_color{0xff808080};         // java Color.GRAY
const juce::Colour g_full_mute_text_border{0xffc0c0c0};     // java Color.LIGHT_GRAY
const juce::Colour g_palm_mute_inner_color{0xff050505};     // palm-mute X fill

// Height of the hand-shape label bar and its bold name text (Charter chartTextHeight).
constexpr float g_shape_label_height{10.0f};
constexpr float g_shape_rail_height{3.0f};
// Chord marks brighten more than arpeggio marks: at the chord multiplier the purple's clamped
// blue channel read too loud next to the blue, so the arpeggio tier sits darker.
constexpr double g_shape_mark_brightness{1.5};
constexpr double g_arpeggio_mark_brightness{1.3};
// Bar width in whole pixels of the square-bracket pair marking an arpeggio posture note, which
// reads as "[ fret ]" and stays much lighter than the note rings it wraps. The brackets draw as
// pixel-snapped rectangles — a fractional width or position antialiases into fuzzy, unsquare
// edges.
constexpr int g_arpeggio_bracket_thickness{2};

// Measures one line of text through a GlyphArrangement layout (JUCE's direct Font string-width
// helpers are deprecated), rounding up so reserved label space never truncates the final glyph.
// Kept private to the paint core; rock-hero-editor/ui/src/shared/text_metrics.{h,cpp} is the
// editor-widget twin of the same measurement.
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

// Thin JUCE-converting wrappers over the shared Charter-exact derivation for the in-file call
// sites that derive from already-opaque colors.
[[nodiscard]] juce::Colour charterDarker(juce::Colour color)
{
    return juce::Colour{darkerColor(color.getARGB())};
}

[[nodiscard]] juce::Colour charterMultiply(juce::Colour color, double multiplier)
{
    return juce::Colour{multiplyColor(color.getARGB(), multiplier)};
}

// Bridges the shared Charter-exact style derivation to JUCE colors at this module's boundary;
// field meanings match common::ui::StringLaneStyle one for one.
struct StringStyle
{
    juce::Colour lane;         // string line: base x0.8
    juce::Colour border_inner; // note ring: lane brightened
    juce::Colour inner;        // note fill: ring darkened twice
    juce::Colour linked_inner; // linked-note fill: the fill darkened twice more
    juce::Colour tail;         // sustain fill: base x0.66
    juce::Colour tail_edge;    // sustain border: tail brightened
    juce::Colour accent;       // accent glow: ring brightened, hue-preserving

    explicit StringStyle(juce::Colour base)
        : StringStyle(StringLaneStyle{base.getARGB()})
    {}

    explicit StringStyle(const StringLaneStyle& style)
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

// Draws one string line per displayed lane across the visible clip, exactly like Charter's lane
// lines: one pixel, the string's base color at 80%.
// Draws the lane lines, leaving gaps over the given per-lane x ranges (indexed by displayed
// string minus one, each lane's ranges in ascending order) so arpeggio "[ fret ]" posture
// marks sit on a clean background instead of the line cutting through them.
void drawStringLines(
    juce::Graphics& g, const TabLaneMetrics& metrics,
    const std::vector<std::vector<juce::Range<float>>>& exclusions)
{
    const juce::Rectangle<int> clip = g.getClipBounds();
    for (int displayed_string = 1; displayed_string <= metrics.displayed_count; ++displayed_string)
    {
        const float y = tabLaneCenterY(displayed_string, metrics.displayed_count, metrics.bounds);
        g.setColour(
            charterMultiply(tabStringColor(displayed_string, metrics.displayed_count), 0.8));
        // Snapped to a whole pixel row so the one-pixel line stays crisp like Charter's.
        const auto row = static_cast<float>(static_cast<int>(y));
        auto cursor = static_cast<float>(clip.getX());
        const auto right = static_cast<float>(clip.getRight());
        for (const juce::Range<float>& range :
             exclusions[static_cast<std::size_t>(displayed_string - 1)])
        {
            const float gap_start = std::min(right, std::max(cursor, range.getStart()));
            if (gap_start > cursor)
            {
                g.fillRect(juce::Rectangle<float>{cursor, row, gap_start - cursor, 1.0f});
            }
            cursor = std::max(cursor, range.getEnd());
        }
        if (cursor < right)
        {
            g.fillRect(juce::Rectangle<float>{cursor, row, right - cursor, 1.0f});
        }
    }
}

// Charter formats bend amounts in whole steps with quarter fractions ("0", "1/2", "1 1/4", ...
// rendered with vulgar-fraction glyphs).
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

// Draws the tremolo tail as a constant-thickness zigzag band: the plain sustain's ribbon with
// its top and bottom borders displaced TOGETHER, so the strip snakes instead of pulsing in
// thickness the way the ported pointed-gem chain did. This matches the 3D highway's teeth,
// which swing a constant-width ribbon the same way.
//
// The band is the plain tail's span grown by half the tremolo size on each side and swung by
// that same half, which pins two things at once: the outer envelope stays exactly the gem
// chain's — the tail occupies the same rows it always has — and the strip's ALWAYS-covered
// core is exactly the plain span, so a slide diagonal, which is drawn to that span, sits
// entirely inside the band at every x instead of crossing its teeth. Apexes come twice per
// gem cell, double the chain's rate, which reads as picking rather than as a slow wave. Drawn
// edge-colored with the tail color inset by the edge size, like every other tail. Vertices run
// past the end and the clip cuts them, so the final tooth keeps its true shape and the strip
// ends on a clean vertical edge.
void drawTremoloTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style, float x,
    float length, float center_y)
{
    const TailSpan span = tailSpan(metrics, center_y);
    const float band_center = (span.top + span.bottom) / 2.0f;
    // Half the tremolo size each way: the swing the band adds to its thickness is the swing it
    // takes back by snaking, so the envelope is the plain span plus the whole tremolo size and
    // the core is the plain span exactly.
    const float amplitude = metrics.tremolo_size / 2.0f;
    const float half_thickness = ((span.bottom - span.top) / 2.0f) + amplitude;
    const float apex_step = std::max(2.0f, metrics.note_height / 4.0f);

    // Triangle wave along the tail, zero at the onset so the band leaves the head centered and
    // alternating apexes every apex_step.
    const auto centerline_at = [&](const float dx) {
        const float cycles = (dx / (2.0f * apex_step)) - 0.25f;
        const float phase = cycles - std::floor(cycles);
        return band_center + (amplitude * (std::abs(phase - 0.5f) - 0.25f) * 4.0f);
    };
    // Vertex 0 sits at the onset; the rest are the apexes, one past the end so the clip can cut
    // the last tooth mid-stroke instead of the path closing short of it.
    const int apex_count = static_cast<int>(length / apex_step) + 2;
    const auto vertex_dx = [&](const int index) {
        return index == 0 ? 0.0f : ((static_cast<float>(index) - 0.5f) * apex_step);
    };

    const auto add_band = [&](juce::Path& path, const float inset) {
        const float half = std::max(1.0f, half_thickness - inset);
        path.startNewSubPath(x, centerline_at(0.0f) - half);
        for (int index = 1; index <= apex_count; ++index)
        {
            const float dx = vertex_dx(index);
            path.lineTo(x + dx, centerline_at(dx) - half);
        }
        for (int index = apex_count; index >= 0; --index)
        {
            const float dx = vertex_dx(index);
            path.lineTo(x + dx, centerline_at(dx) + half);
        }
        path.closeSubPath();
    };

    g.saveState();
    const float reach = half_thickness + amplitude;
    g.reduceClipRegion(
        juce::Rectangle<float>{x, band_center - reach, length, 2.0f * reach}
            .getSmallestIntegerContainer());
    juce::Path edge_band;
    add_band(edge_band, 0.0f);
    g.setColour(style.tail_edge);
    g.fillPath(edge_band);

    juce::Path inner_band;
    add_band(inner_band, metrics.tail_edge_size);
    g.setColour(style.tail);
    g.fillPath(inner_band);
    g.restoreState();
}

// Draws the sustain tail: Charter's filled bar with a brighter stroked border, the tremolo gem
// strip variant, and the vibrato sine overlay.
void drawNoteTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::TabNoteView& note, float onset_x, float center_y)
{
    const float end_x = metrics.x(note.end_seconds);
    const float length = end_x - onset_x;
    if (length <= 0.0f)
    {
        return;
    }

    const TailSpan span = tailSpan(metrics, center_y);
    // A scrape rides the tremolo strip outright: the teeth MEAN unmeasured noise (the
    // charting standard spells out measured repetition as discrete notes), and a scrape is
    // that noise dragged along the string — the slide diagonals over it carry the travel.
    const bool scrape = note.attack == common::core::NoteAttack::PickSlide;
    if (note.tremolo || scrape)
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

// The note-head silhouettes. The shape carries what KIND of note this is; it never carries which
// hand produced it, which is what a present mark's DARKNESS says instead. So the plectrum names a
// pick scrape while the head itself keeps the ordinary string colors, and only the beside-head
// chip goes dark.
enum class HeadShape
{
    Round,
    Diamond,
    Plectrum
};

// Picks the silhouette naming this note's kind. The harmonic diamond takes precedence over the
// scrape's plectrum only so the mapping is total: no note can ask for both, since a pinch and a
// scrape are two values of one attack and the chart rules reject a scrape carrying a node.
[[nodiscard]] HeadShape headShapeFor(const common::core::TabNoteView& note)
{
    if (note.harmonic_node.has_value())
    {
        return HeadShape::Diamond;
    }
    if (note.attack == common::core::NoteAttack::PickSlide)
    {
        return HeadShape::Plectrum;
    }
    return HeadShape::Round;
}

// Half of the plectrum silhouette, measured off the pick-slide cell of the shipped note atlas
// (cell 9, g_head_cell_pick_slide) at its 0.5-coverage line — the same level the atlas's own
// fracture is pinned to — in units of the head's extent, with the silhouette's box center at the
// origin.
//
// Only the RIGHT half is stored, as the chain from the blunt top edge's right corner down to the
// tip. The art is mirror-symmetric to the last measured sample (every boundary sample's mirror
// lands on another sample, worst distance 0.000000 px), so mirroring this chain at draw time makes
// the two sides exact by construction instead of asking two authored halves to agree.
//
// The x values carry the aspect. The cell measures 31.000 x 32.997 px at that level, so scaling
// BOTH axes by the extent fits the silhouette's HEIGHT to the extent and leaves its width at
// 0.9395 of it, the art's own proportion. The head therefore stands exactly as tall as the round
// head it replaces and 6% narrower, which leaves the lane's vertical collision budget alone.
//
// A rounded triangle is not a substitute for the table: the silhouette keeps widening for nine
// rows below its topmost ink, and its widest row sits 0.1515 of the height ABOVE the box center,
// so its mass is upper-heavy in a way no three-corner rounded triangle reproduces. Sixteen points
// hold the measured outline to 0.0898 px at a 25 px note height and 0.0449 px at 12.
constexpr std::array<juce::Point<float>, 16> g_plectrum_half_outline{
    juce::Point<float>{0.03031f, -0.50000f}, // the blunt top edge's right corner
    juce::Point<float>{0.12122f, -0.49511f},
    juce::Point<float>{0.24245f, -0.46738f},
    juce::Point<float>{0.33336f, -0.43214f},
    juce::Point<float>{0.36367f, -0.40898f},
    juce::Point<float>{0.43366f, -0.33332f},
    juce::Point<float>{0.46071f, -0.27271f},
    juce::Point<float>{0.46821f, -0.24240f},
    juce::Point<float>{0.46974f, -0.15148f}, // widest row
    juce::Point<float>{0.46054f, -0.09087f},
    juce::Point<float>{0.40451f, 0.03035f},
    juce::Point<float>{0.31390f, 0.18188f},
    juce::Point<float>{0.25008f, 0.27280f},
    juce::Point<float>{0.08321f, 0.45463f},
    juce::Point<float>{0.03031f, 0.49559f},
    juce::Point<float>{0.00000f, 0.50000f}, // the tip, on the mirror axis
};

// Builds the plectrum outline as a closed path at one extent: down the measured right half from
// the top edge's right corner to the tip, then back up its mirror image, so the two sides cannot
// disagree. The tip is shared and closing the path draws the blunt top edge between the two top
// corners, for 31 vertices in all.
//
// An extent at or below zero yields an empty path. Scaling this outline by a negative extent would
// not shrink it, it would turn the plectrum upside down, because the silhouette is not centrally
// symmetric — unlike the disc and the diamond, which a negative extent merely mirrors onto
// themselves. Small lanes reach that: the innermost layer's extent is size - 4 * border, which
// goes negative once the head is under 5 px.
[[nodiscard]] juce::Path plectrumPath(float center_x, float center_y, float extent)
{
    juce::Path shape;
    if (extent <= 0.0f)
    {
        return shape;
    }

    const auto vertex = [&](const juce::Point<float>& outline_point, float x_sign) {
        return juce::Point<float>{
            center_x + (x_sign * outline_point.x * extent), center_y + (outline_point.y * extent)
        };
    };

    shape.startNewSubPath(vertex(g_plectrum_half_outline.front(), 1.0f));
    for (std::size_t i = 1; i < g_plectrum_half_outline.size(); ++i)
    {
        shape.lineTo(vertex(g_plectrum_half_outline[i], 1.0f));
    }
    // Back up the mirrored side, skipping the tip the two halves share.
    for (std::size_t i = g_plectrum_half_outline.size() - 1; i-- > 0;)
    {
        shape.lineTo(vertex(g_plectrum_half_outline[i], -1.0f));
    }
    shape.closeSubPath();
    return shape;
}

// Fills Charter's layered note-head shape: a dark outer ring, a bright string-colored ring, and
// a colored center (dimmed for normal heads, doubly dimmed for linked heads). Harmonic notes use
// the diamond silhouette of the same layers, pick scrapes the plectrum's.
//
// The layers are concentric by SCALE rather than by a true offset, so the visible ring between two
// of them is `border` wide only where the outline faces the center squarely. Its tightest
// perpendicular gap is 2 * border * (the shape's smallest center-to-edge distance, in units of its
// height): 1.0000 * border for the disc, 0.7228 for the plectrum, 0.7071 for the diamond already
// shipping beside it. The plectrum's rings are therefore the family's middle case, 1.0222x the
// diamond's — 1.2529 px against 1.2257 px at a 25 px note height.
void fillHeadShape(
    juce::Graphics& g, juce::Colour border_inner, juce::Colour inner, float center_x,
    float center_y, float size, HeadShape shape)
{
    const float border = std::max(1.0f, size / 15.0f);

    const auto layer = [&](float inset, juce::Colour color) {
        const float extent = size - 2.0f * inset;
        g.setColour(color);
        switch (shape)
        {
            case HeadShape::Diamond:
            {
                juce::Path diamond;
                diamond.startNewSubPath(center_x, center_y - extent / 2.0f);
                diamond.lineTo(center_x + extent / 2.0f, center_y);
                diamond.lineTo(center_x, center_y + extent / 2.0f);
                diamond.lineTo(center_x - extent / 2.0f, center_y);
                diamond.closeSubPath();
                g.fillPath(diamond);
                break;
            }
            case HeadShape::Plectrum:
            {
                g.fillPath(plectrumPath(center_x, center_y, extent));
                break;
            }
            case HeadShape::Round:
            {
                g.fillEllipse(center_x - extent / 2.0f, center_y - extent / 2.0f, extent, extent);
                break;
            }
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
    const common::core::TabNoteView& note, float onset_x, float center_y,
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
    for (const common::core::TabSlideView& waypoint : note.slides)
    {
        const float to_x = metrics.x(waypoint.seconds) - line_thickness;
        // A hold segment (same fret) is a tie, not a glide: no diagonal — the linked head at
        // the waypoint renders the continuation, and the next segment's line leaves from here.
        if (waypoint.fret == previous_fret)
        {
            from_x = to_x;
            continue;
        }
        const bool upward = waypoint.fret >= previous_fret;
        const float from_y =
            upward ? span.bottom - line_thickness / 2.0f : span.top + line_thickness / 2.0f;
        const float to_y =
            upward ? span.top + line_thickness / 2.0f : span.bottom - line_thickness / 2.0f;

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
// its fret number at each linked slide waypoint. Charter charts express unpicked slide chains as
// linked notes and draw one of these at every link; our format merges the chain into waypoints,
// so the linked waypoints are exactly where Charter's linked heads sit. A shift slide's landing
// is not linked — the re-picked target note's own head renders there instead, and painting the
// linked head over it would make a picked note look like a continuation.
void drawSlideWaypointHeads(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::TabNoteView& note, float center_y)
{
    for (const common::core::TabSlideView& waypoint : note.slides)
    {
        if (waypoint.unpitched || !waypoint.linked)
        {
            continue;
        }

        const float x = metrics.x(waypoint.seconds);
        const float size = metrics.note_height + 1.0f;
        fillHeadShape(
            g, style.border_inner, style.linked_inner, x, center_y, size, HeadShape::Round);
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
// over the tail, then a flat run to the tail end, with a "<slur><amount>" chip at each bend
// point (white text on the string's lane color darkened twice).
void drawBendLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::TabNoteView& note, float onset_x, float center_y,
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
    for (const common::core::TabBendPointView& point : note.bend)
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
//
// The plectrum shares the disc's radial fade, stated rather than defaulted even though the chart
// rules make an accented scrape unreachable. The fade band clears the plectrum's diagonal
// shoulder by only 0.331 px at a 25 px head against the disc's 1.560; the fix is glow_size, which
// the round head shares, so it stays as it is until an accented scrape is legal (the measurements
// live with the deferred cell in the technique-compatibility plan doc).
void drawAccentGlow(
    juce::Graphics& g, const StringStyle& style, float center_x, float center_y, float size,
    HeadShape shape)
{
    const float glow_size = size * 1.4f;
    if (shape == HeadShape::Diamond)
    {
        // Concentric fading diamond outlines approximate Charter's diamond-distance fade.
        for (int ring = 0; ring < 4; ++ring)
        {
            const float extent = glow_size * (0.8f + 0.05f * static_cast<float>(ring));
            juce::Path outline;
            outline.startNewSubPath(center_x, center_y - extent / 2.0f);
            outline.lineTo(center_x + extent / 2.0f, center_y);
            outline.lineTo(center_x, center_y + extent / 2.0f);
            outline.lineTo(center_x - extent / 2.0f, center_y);
            outline.closeSubPath();
            g.setColour(style.accent.withAlpha(1.0f - 0.25f * static_cast<float>(ring)));
            g.strokePath(outline, juce::PathStrokeType{glow_size * 0.05f});
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
        mute == common::core::NoteMute::Full ? juce::Colours::white : g_palm_mute_inner_color;
    g.setColour(inner);
    g.fillPath(x_shape);
    g.setColour(g_mute_border_color);
    g.strokePath(x_shape, juce::PathStrokeType{std::max(1.0f, space / 3.0f)});
}

// The lettered plate's side, as a fraction of the note height: big enough to hold the fret
// number's own font, small enough to stay under half the head's diameter.
constexpr float g_letter_badge_fraction = 0.55f;

// A capital's ink height as a fraction of the JUCE font height it was asked for. JUCE's height
// is the ascent-plus-descent line box, not a cap height, so a capital fills only about half of
// it; the plate is sized against the ink rather than the number.
constexpr float g_capital_ink_fraction = 0.55f;

// Hairline the attack mark keeps clear of the fret number's ink. It is the only slack in the
// mark's placement: everything else about where a mark sits is derived from the head and the
// number already drawn on it.
constexpr float g_icon_slot_gap = 1.0f;

// Optical correction for a mark whose LOWEST ink stops short of its box's right edge, as a
// fraction of how far short it stops. Such a mark reads FURTHER from the head than one whose
// lowest ink reaches that edge, even when the two boxes are level, because the head sits
// below-and-right and the eye weights the boundary nearest it.
//
// The factor is calibrated, not guessed. Of the metrics tried against the eye's verdict -
// overlap area, closest approach, area centroid, and a blurred-image (squint-test) product -
// only a radial gap reproduced it: cast rays out from the head's center, take the Euclidean
// distance from the rim to the first ink each ray meets, and average over the mark's own
// angular span, clipping deep recesses the way HT Letterspacer clips margin depth. Every other
// metric ranked the plate outside the two triangles, which is not what the eye reports.
// Equalizing that gap put the correction at 0.5 of the inset (measured 0.506 at a note height
// of 25 and 0.507 at 20; the residual spread across the three marks falls from 2.25 px to
// 0.24 px), and it reproduces zero for the plate and the pull-off, which must not move.
constexpr float g_optical_inset_correction = 0.5f;

// Draws the legato triangle beside the head: hammer-on points down, pull-off up. The triangle
// means LEGATO — the note is not re-attacked — which is why it carries no letter: the
// direction is the whole message, and it is the mnemonic a guitarist already has (hammer down
// onto the string, pull off it). Attacks that ARE re-picked wear a lettered plate instead.
void drawTriangleIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, float center_x, float center_y,
    bool pointing_down, juce::Colour fill, juce::Colour border)
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
}

// Draws a picking-hand attack as its standard tab letter on a rounded plate.
//
// The plate's PARALLEL SIDES are the whole point. A triangle tapers, so at the height where a
// capital's ink sits it retains barely half its width — the letters slap and pop used to carry
// overflowed their own badges at every lane size. A square holds the letter at the badge's full
// width, so the three picking-hand attacks can share one silhouette and let the letter name
// them, which is what a guitarist reads anyway (T, S, P).
//
// The letter is the fret number's own font, so it is exactly as legible as the digit the reader
// is already reading — no separate size to tune. It draws only when the plate can hold its ink:
// JUCE's font "height" is the ascent-plus-descent line box rather than a cap height (verified
// in juce_Typeface.cpp, getPointsToHeightFactor() = ascent + descent), so a capital's ink is
// only about half the number the font was asked for, and a plate smaller than that ink would
// spill the letter over its edges the way the triangles did.
void drawLetterPlate(
    juce::Graphics& g, const TabLaneMetrics& metrics, juce::Rectangle<float> plate,
    const juce::String& letters)
{
    const float border = std::max(1.0f, plate.getHeight() / 9.0f);
    const float radius = plate.getHeight() * 0.22f;

    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(plate, radius);
    g.setColour(g_full_mute_text_border);
    g.drawRoundedRectangle(plate, radius, border);

    const float ink = metrics.fret_font.getHeight() * g_capital_ink_fraction;
    if (metrics.draw_text && plate.getHeight() >= ink + (2.0f * border))
    {
        g.setColour(juce::Colours::white);
        g.setFont(metrics.fret_font);
        g.drawText(letters, plate, juce::Justification::centred);
    }
}

// The square single-capital case the three picking-hand attacks share.
void drawLetterBadge(
    juce::Graphics& g, const TabLaneMetrics& metrics, float center_x, float center_y,
    const juce::String& letter)
{
    const float side = metrics.note_height * g_letter_badge_fraction;
    drawLetterPlate(
        g,
        metrics,
        juce::Rectangle<float>{center_x - side / 2.0f, center_y - side / 2.0f, side, side},
        letter);
}

// Clear pixels the chip keeps between its letters' ink and the INNER edge of its rim. Measured from
// the inner edge because the rim is a CENTERED stroke: padding measured from the box edge instead
// left a tenth of a pixel of fill between letter and rim, which antialiased into one merged run.
constexpr float g_chip_letter_clearance = 1.0f;

// Width of the pick-scrape chip: exactly what its letters need. The rim is centered on the box
// edge, so it eats `border` of interior across the two sides, then the clearance on each side.
//
// This is the whole reason the mark reads "PS" and not "P.S.": the slot has about 21 px of clear
// width before the previous sixteenth note's head at the shipped lane, and no four-glyph string
// fits it — "P.S." needs 26.5 px under this rule, and the two periods alone cost 8.6.
[[nodiscard]] float chipWidth(
    const TabLaneMetrics& metrics, const juce::String& letters, float height)
{
    const float border = std::max(1.0f, height / 9.0f);
    return static_cast<float>(textWidth(metrics.fret_font, letters)) + border +
           (2.0f * g_chip_letter_clearance);
}

// Draws the attack technique icon beside the head. The vocabulary reads by SHAPE, and the shape
// says what the attack IS: a triangle means legato — the note is not re-attacked — so it needs
// no letter, only a direction (down for a hammer-on, up for a pull-off). Everything that IS
// re-attacked by the picking hand wears a lettered plate carrying the letter printed tab
// already uses (T, S, P), so those three share one silhouette and the letter names them. The pick
// slide takes the same plate in the same slot, just two letters wide.
//
// Its chip is deliberately redundant with the plectrum head under it: both say "pick scrape". The
// slot cannot be contested — `attack` is a single field and the chart rules forbid a scrape any
// other technique — so the pair can only ever agree, and saying it twice is what makes it plain.
//
// Every beside-head mark tucks into the head's UPPER-LEFT shoulder, and they all pin the same
// point: the bottom-right corner of the mark's own box. Pinning a corner rather than a center
// puts a wide lettered plate and a narrow triangle in the same place relative to the head, so
// the family reads as one slot however the individual marks are shaped or the lane is sized.
//
// The corner slides down the diagonal until the LOOSEST mark in the family reaches the head.
// That mark is the hammer-on triangle, whose box corner is empty because its apex is
// bottom-CENTER: it meets the head half a width later than any mark whose corner is solid.
// Placing that apex on the rim and letting the rest of the family keep the same corner is what
// buys one slot — the price, paid deliberately, is that the solid-cornered marks press into
// the head's colored ring rather than resting against it. Going up as well as left also clears
// the sustain ribbon arriving from the previous note, which a mark level with the head sits on.
//
// That same empty box corner then costs the hammer-on twice, and the second cost is optical:
// with all three boxes level, it READS as further from the head than the other two. Only the
// hammer-on needs the correction below, because only its lowest ink stops short of its box.
void drawAttackIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::TabNoteView& note,
    float center_x, float center_y)
{
    // The apex sits at (corner_x - width/2, corner_y) and the corner is tuck from the head's
    // center on both axes, so putting the apex on the rim is solving
    // 2*tuck^2 + width*tuck + width^2/4 - radius^2 = 0. The head is drawn one pixel larger than
    // the note height so it gets a center pixel on the string line (see drawNoteHead).
    const float radius = (metrics.note_height + 1.0f) / 2.0f;
    const float triangle_width = metrics.note_height / 2.0f;
    float tuck =
        (std::sqrt((8.0f * radius * radius) - (triangle_width * triangle_width)) - triangle_width) /
        4.0f;
    if (metrics.draw_text)
    {
        // Floored so no mark can reach the fret number. Half a capital's ink OVER-states how
        // far the digits climb above the lane center, because JUCE centers the line box and the
        // digits sit on a baseline below that center (juce_GlyphArrangement.cpp, justifyGlyphs
        // puts the baseline at center minus height/2 plus ascent), so the margin errs safe. The
        // floor binds only at the small end, where the font stops shrinking with the head.
        const float ink_reach = metrics.fret_font.getHeight() * g_capital_ink_fraction / 2.0f;
        tuck = std::max(tuck, ink_reach + g_icon_slot_gap);
    }
    const float corner_x = center_x - tuck;
    const float corner_y = center_y - tuck;
    const auto mark_center = [&](const float width, const float height) {
        return juce::Point<float>{corner_x - (width / 2.0f), corner_y - (height / 2.0f)};
    };
    const juce::Point<float> triangle =
        mark_center(metrics.note_height / 2.0f, metrics.note_height * 2.0f / 5.0f);
    const float badge_side = metrics.note_height * g_letter_badge_fraction;
    const juce::Point<float> badge = mark_center(badge_side, badge_side);

    switch (note.attack)
    {
        case common::core::NoteAttack::Hammer:
        {
            // The apex is this mark's lowest ink and stops half a triangle-width short of its
            // box's right edge; g_optical_inset_correction is the share of that shortfall the
            // eye needs back to read it level with the marks whose lowest ink reaches the edge.
            const float apex_inset = triangle_width / 2.0f;
            drawTriangleIcon(
                g,
                metrics,
                triangle.x + (apex_inset * g_optical_inset_correction),
                triangle.y,
                true,
                juce::Colours::white,
                juce::Colours::black);
            break;
        }
        case common::core::NoteAttack::Pull:
        {
            drawTriangleIcon(
                g,
                metrics,
                triangle.x,
                triangle.y,
                false,
                juce::Colours::white,
                juce::Colours::black);
            break;
        }
        case common::core::NoteAttack::Tap:
        {
            drawLetterBadge(g, metrics, badge.x, badge.y, "T");
            break;
        }
        case common::core::NoteAttack::Slap:
        {
            drawLetterBadge(g, metrics, badge.x, badge.y, "S");
            break;
        }
        case common::core::NoteAttack::Pop:
        {
            drawLetterBadge(g, metrics, badge.x, badge.y, "P");
            break;
        }
        case common::core::NoteAttack::PickSlide:
        {
            // The letters carry the identity, so the chip's width is reserved for them at EVERY
            // size, including sizes too small to draw them: the aspect ratio is all that survives
            // down there, and it must never collapse toward the square badge the tap, slap and pop
            // plates share. Its lowest ink reaches the box's right edge, so it needs none of the
            // hammer-on's optical correction.
            const juce::String letters{"PS"};
            const float width = chipWidth(metrics, letters, badge_side);
            const juce::Point<float> chip = mark_center(width, badge_side);
            drawLetterPlate(
                g,
                metrics,
                juce::Rectangle<float>{
                    chip.x - (width / 2.0f), chip.y - (badge_side / 2.0f), width, badge_side
                },
                letters);
            break;
        }
        case common::core::NoteAttack::Pinch:
        {
            // Nothing here: a pinch's mark is the bar drawn beside the diamond head with the head
            // itself, not a plate in this band. It reads as a harmonic cue rather than an attack
            // cue even though the data now lives on the attack.
            break;
        }
        case common::core::NoteAttack::Pick:
        {
            break;
        }
    }
}

// How far the fret number rides above the string line on a plectrum head, as a fraction of the
// head. The plectrum is upper-heavy — its widest row sits 0.1515 of the head above the box center
// and it tapers to a tip below — so a digit centered on the line straddles the narrowing half. The
// raise moves it onto the broad band, and the beside-head chip is what caps it: the chip's lower
// edge sits 5.520 px above the line at a 25 px note height, and this is the largest raise that
// still leaves the digit's ink all but clear of it.
//
// The number is NOT boxed here. A plate would answer a question the silhouette has already
// answered, and the raise buys the same legibility from the art itself.
constexpr float g_plectrum_digit_raise = 0.1154f;

// Draws the complete note head stack in Charter's order: accent glow, layered head shape, the
// pinch-harmonic edge line, mute icon, fret number, then the attack icon.
void drawNoteHead(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::TabNoteView& note, float onset_x, float center_y)
{
    // Charter renders heads one pixel larger than the configured height so they get a center
    // pixel on the string line.
    const float size = metrics.note_height + 1.0f;
    const HeadShape shape = headShapeFor(note);

    if (note.accent)
    {
        drawAccentGlow(g, style, onset_x, center_y, size, shape);
    }

    fillHeadShape(g, style.border_inner, style.inner, onset_x, center_y, size, shape);

    if (note.attack == common::core::NoteAttack::Pinch)
    {
        const float line_x = onset_x - metrics.note_height / 2.0f;
        g.setColour(style.border_inner);
        g.fillRect(
            juce::Rectangle<float>{
                line_x - 1.5f, center_y - metrics.note_height / 2.0f, 3.0f, metrics.note_height
            });
    }

    // The X reports this note's OWN mute state and nothing else — a mark that means one thing on
    // one note and another elsewhere is a mark the reader has to disambiguate; the plectrum
    // silhouette already says what a scrape is. The chart rules reject a mute on a pick-slide
    // note outright, so a scrape passes None here and draws no X at all.
    drawMuteIcon(g, metrics, note.mute, onset_x, center_y);

    if (metrics.draw_text)
    {
        const juce::String head_text = tabNoteHeadText(note);
        if (note.mute != common::core::NoteMute::None)
        {
            // Charter boxes the fret number on full mutes so it stays readable over the X;
            // palm mutes need the same plate (the X's crossing strokes cut through the digits),
            // in the palm X's own colors so it reads as the X's center.
            const bool full_mute = note.mute == common::core::NoteMute::Full;
            const auto text_width = static_cast<float>(textWidth(metrics.fret_font, head_text));
            const juce::Rectangle<float> box{
                onset_x - text_width / 2.0f - 2.0f,
                center_y - metrics.fret_font.getHeight() / 2.0f - 1.0f,
                text_width + 4.0f,
                metrics.fret_font.getHeight() + 2.0f
            };
            g.setColour(full_mute ? g_mute_border_color : g_palm_mute_inner_color);
            g.fillRect(box);
            g.setColour(full_mute ? g_full_mute_text_border : g_mute_border_color);
            g.drawRect(box, 1.0f);
        }
        // Only the plectrum moves its digit: the disc and the diamond are widest on the string
        // line, so their numbers stay centered on it.
        const float digit_raise =
            shape == HeadShape::Plectrum ? g_plectrum_digit_raise * size : 0.0f;
        g.setColour(juce::Colours::white);
        g.setFont(metrics.fret_font);
        g.drawText(
            head_text,
            juce::Rectangle<float>{
                onset_x - size, center_y - size - digit_raise, size * 2.0f, size * 2.0f
            },
            juce::Justification::centred);
    }

    drawAttackIcon(g, metrics, note, onset_x, center_y);
}

// Draws one hand-shape span as narrow rails along the lane's top and bottom edges for the
// span's duration — blue for chord shapes, purple for arpeggios — echoing the 3D highway's
// shape rails at the hand-window fret lines (a departure from Charter's full-height tint, which
// read as an ugly wall of color). The template name, when present, rides the host's name-chip
// band (the editor's timeline ruler), not the lane itself.
void drawShapeSpan(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::TabShapeView& shape)
{
    const float start_x = metrics.x(shape.start_seconds);
    const float end_x = metrics.x(shape.end_seconds);
    if (end_x <= start_x)
    {
        return;
    }

    const juce::Colour color = tabShapeMarkColor(shape.arpeggio);
    const float width = end_x - start_x;
    const float bottom_rail_y =
        static_cast<float>(metrics.bounds.getBottom()) - g_shape_rail_height;
    g.setColour(color);
    g.fillRect(
        juce::Rectangle<float>{
            start_x, static_cast<float>(metrics.bounds.getY()), width, g_shape_rail_height
        });
    g.fillRect(juce::Rectangle<float>{start_x, bottom_rail_y, width, g_shape_rail_height});

    // The template name is not drawn here: the same tab projection feeds the editor timeline
    // ruler's shape-label band, which shows the name directly above this span in the ruler's
    // vertical space; the lane itself has no clean room for names.
}

// Draws one fret-hand-position marker: a small boxed fret label along the lane's top edge.
// This presentation is ours, not Charter's (Charter shows FHPs in a separate strip above the
// lanes, which this single-row lane does not have); it stays deliberately unobtrusive until the
// FHP display treatment is decided. The standard four-fret hand shows just the index-finger
// fret; a wider or narrower placement spells out its full inclusive range ("3-7") because the
// unusual span is exactly what the player needs to see.
void drawFhpMarker(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::TabFhpView& fhp)
{
    if (!metrics.draw_text)
    {
        return;
    }

    const float marker_x = metrics.x(fhp.seconds);
    const juce::String text =
        fhp.width == 4 ? juce::String{fhp.fret}
                       : juce::String{fhp.fret} + "-" + juce::String{fhp.fret + fhp.width - 1};
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

// Converts the shared palette authority to JUCE colors at the paint core's boundary; the
// lane-window logic lives with the palette (string_color_palette.h).
juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return juce::Colour{stringLaneColor(
        displayed_string, displayed_string_count, charterClassicPalette())};
}

// Rationale lives on the declaration in tab_paint_core.h.
juce::String tabNoteHeadText(const common::core::TabNoteView& note)
{
    if (!note.harmonic_node.has_value() || !common::core::nodeIsOnNeck(note.attack))
    {
        return juce::String{note.fret};
    }
    juce::String text{*note.harmonic_node, 1};
    if (text.endsWith(".0"))
    {
        text = text.dropLastCharacters(2);
    }
    return text;
}

// Shared with host name chips (the editor timeline ruler's chord/arpeggio band) so chip and
// rails always agree (brightness bumps over the Charter hand-shape bases).
juce::Colour tabShapeMarkColor(bool arpeggio)
{
    return arpeggio ? charterMultiply(g_hand_shape_arpeggio_color, g_arpeggio_mark_brightness)
                    : charterMultiply(g_hand_shape_color, g_shape_mark_brightness);
}

// Base color for a chart string, accounting for extra user lanes below the chart.
juce::Colour TabLaneMetrics::baseColor(int chart_string) const
{
    return tabStringColor(chart_string + extra_lanes, displayed_count);
}

TabLaneMetrics makeTabLaneMetrics(
    juce::Rectangle<int> bounds, common::core::TimeRange visible_timeline, int displayed_count,
    int chart_string_count, TabLaneStyle style)
{
    TabLaneMetrics metrics;
    static_cast<TabLaneGeometry&>(metrics) = makeTabLaneGeometry(
        static_cast<float>(bounds.getX()),
        static_cast<float>(bounds.getY()),
        static_cast<float>(bounds.getWidth()),
        static_cast<float>(bounds.getHeight()),
        visible_timeline,
        displayed_count,
        chart_string_count,
        style);
    metrics.bounds = bounds;
    metrics.fret_font =
        juce::Font{juce::FontOptions{std::max(8.0f, metrics.note_height / 2.0f)}.withStyle("Bold")};
    metrics.bend_font = juce::Font{juce::FontOptions{std::max(10.0f, metrics.note_height / 4.0f)}};
    metrics.label_font = juce::Font{juce::FontOptions{g_shape_label_height}.withStyle("Bold")};
    return metrics;
}

// Draws the visible chart content in Charter's layer order: string lines, hand-shape spans,
// sustain tails with their slide and bend lines, arpeggio posture brackets, note heads with
// technique glyphs, then the floating labels (slide frets and bend amount chips) on top.
void paintTabLane(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::TabViewState& tab,
    const std::vector<double>& prefix_max_end_seconds)
{
    // Heads and icons extend a fixed pixel slack around their onset, so the visible span grows
    // by that slack before the visibility queries.
    const juce::Rectangle<int> clip = g.getClipBounds();
    const double duration = metrics.visible_timeline.duration().seconds;
    const double seconds_per_pixel = duration / static_cast<double>(metrics.bounds.getWidth());
    const double slack_seconds =
        static_cast<double>(metrics.max_note_height) * 3.0 * seconds_per_pixel;
    const double span_start = metrics.visible_timeline.start.seconds +
                              static_cast<double>(clip.getX()) * seconds_per_pixel - slack_seconds;
    const double span_end = metrics.visible_timeline.start.seconds +
                            static_cast<double>(clip.getRight()) * seconds_per_pixel +
                            slack_seconds;

    // Bracket geometry shared by the string-line gaps below and the bracket pass further
    // down; the values depend only on the lane metrics, not on the individual note.
    const float bracket_size = metrics.note_height + 1.0f;
    const float bracket_border = std::max(1.0f, bracket_size / 15.0f);
    const float bracket_radius = bracket_size / 2.0f + bracket_border;

    // The lane lines hide inside each visible arpeggio bracket so the "[ fret ]" marks read
    // on a clean background.
    std::vector<std::vector<juce::Range<float>>> line_exclusions(
        static_cast<std::size_t>(metrics.displayed_count));
    for (const common::core::TabShapeView& shape : tab.shapes)
    {
        if (!shape.arpeggio || shape.start_seconds < span_start || shape.start_seconds > span_end)
        {
            continue;
        }

        const float start_x = metrics.x(shape.start_seconds);
        for (const common::core::TabArpeggioNoteView& arpeggio_note : shape.arpeggio_notes)
        {
            const int displayed = arpeggio_note.string + metrics.extra_lanes;
            if (displayed >= 1 && displayed <= metrics.displayed_count)
            {
                line_exclusions[static_cast<std::size_t>(displayed - 1)].emplace_back(
                    start_x - bracket_radius, start_x + bracket_radius);
            }
        }
    }

    drawStringLines(g, metrics, line_exclusions);

    for (const common::core::TabShapeView& shape : tab.shapes)
    {
        if (shape.end_seconds >= span_start && shape.start_seconds <= span_end)
        {
            drawShapeSpan(g, metrics, shape);
        }
    }

    const auto [first, last] =
        tabVisibleNoteRange(tab.notes, prefix_max_end_seconds, span_start, span_end);

    // Floating labels collected during the note passes and drawn above every head.
    std::vector<LabelChip> slide_labels;
    std::vector<LabelChip> bend_chips;

    // Tails first so heads always cover their own tail starts (Charter's noteTails layer).
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::TabNoteView& note = tab.notes[index];
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

    // Arpeggio spans draw "( fret )" bracket marks around every posture string at the
    // bracket start. Onsets carry no vertical bars — the heads themselves already mark them, so
    // the span rails and these brackets are the only shape furniture.
    for (const common::core::TabShapeView& shape : tab.shapes)
    {
        if (!shape.arpeggio || shape.start_seconds < span_start || shape.start_seconds > span_end)
        {
            continue;
        }

        const float start_x = metrics.x(shape.start_seconds);
        for (const common::core::TabArpeggioNoteView& arpeggio_note : shape.arpeggio_notes)
        {
            // Left and right square brackets hugging the head's ring, in the head's muted
            // interior color so they mark the posture without competing with real heads. A
            // string sounded exactly at the start keeps its full head (drawn by the note
            // pass) inside the brackets; a string struck later in the arpeggio shows only its
            // held fret number between them.
            const StringStyle style{metrics.baseColor(arpeggio_note.string)};
            const float center_y = metrics.laneY(arpeggio_note.string);
            // The note's VISIBLE top and bottom are the bright ring's edges: the head's
            // outermost layer is the near-black backing, which melts into the dark lane. The
            // brackets stop a bar-width inside that visible edge, so they never rise above or
            // dip below what reads as the note. Every rectangle snaps to whole pixels so the
            // brackets stay perfectly square instead of antialiasing into fuzz.
            const float half_height = bracket_size / 2.0f - bracket_border -
                                      static_cast<float>(g_arpeggio_bracket_thickness);
            const int bar = g_arpeggio_bracket_thickness;
            const int serif = juce::roundToInt(bracket_size / 8.0f) + bar;
            const int top = juce::roundToInt(center_y - half_height);
            const int bottom = juce::roundToInt(center_y + half_height);
            const int left =
                juce::roundToInt(start_x - bracket_radius - static_cast<float>(bar) / 2.0f);
            const int right =
                juce::roundToInt(start_x + bracket_radius + static_cast<float>(bar) / 2.0f);

            g.setColour(style.inner);
            g.fillRect(left, top, bar, bottom - top);
            g.fillRect(left, top, serif, bar);
            g.fillRect(left, bottom - bar, serif, bar);
            g.fillRect(right - bar, top, bar, bottom - top);
            g.fillRect(right - serif, top, serif, bar);
            g.fillRect(right - serif, bottom - bar, serif, bar);

            if (!arpeggio_note.sounded && metrics.draw_text)
            {
                // The same centering box drawNoteHead uses for a plain fret number.
                g.setColour(juce::Colours::white);
                g.setFont(metrics.fret_font);
                g.drawText(
                    juce::String{arpeggio_note.fret},
                    juce::Rectangle<float>{
                        start_x - bracket_size,
                        center_y - bracket_size,
                        bracket_size * 2.0f,
                        bracket_size * 2.0f
                    },
                    juce::Justification::centred);
            }
        }
    }

    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::TabNoteView& note = tab.notes[index];
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

    for (const common::core::TabFhpView& fhp : tab.fret_hand_positions)
    {
        if (fhp.seconds >= span_start && fhp.seconds <= span_end)
        {
            drawFhpMarker(g, metrics, fhp);
        }
    }
}

} // namespace rock_hero::common::ui
