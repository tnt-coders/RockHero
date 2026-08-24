#include "tab/tab_paint_core.h"

#include "string_colors/string_color_palette.h"
#include "tab/plectrum_outline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
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
const juce::Colour g_palm_mute_inner_color{0xff050505};     // palm-mute X fill

// The lane's hand axis (55-Q1): a mark's hand signature is its FILL POLARITY, not its shape —
// dark ink marks the picking hand, light ink the fretting hand, exactly as the legato triangles
// already draw white-on-black. The letter names the gesture and the polarity names the hand,
// which is how the right-hand tap (dark T) and the left-hand tap (light T) share one letter
// without colliding.
enum class Hand : std::uint8_t
{
    Picking,
    Fretting
};

// One plate's two inks, chosen by the hand axis above; every plate shares one rim.
struct PlatePalette
{
    // Plate body fill.
    juce::Colour fill;

    // Letter ink.
    juce::Colour ink;
};

// Every plate's rim, both hands. An outline is only as visible as its POORER neighbour, and this
// rim has two: the plate's own fill inside, the lane outside. Equalising those two contrasts is
// what balances the polarities, and it is not the same thing as equalising the rim against its
// fill alone: the ink-matched rim that read as a blazing outline on the dark plate and no outline
// at all beside the light one scores a perfect zero on that rule (100 either way in CIE L*), yet
// against the LANE it measures L* 95.3 on one hand and 4.7 on the other.
//
// The weakest side is therefore largest, and equal for both hands, where rim-to-lane equals
// rim-to-white-fill: L* (100 + 4.68) / 2 = 52.34, which is this grey (L* 52.41). Measured weakest
// sides are 47.7 dark and 47.6 light, against 48.9 / 46.4 for the mid-grey that preceded it.
const juce::Colour g_plate_rim{0xff7d7d7d};

// Defined below the ink set it reads from; see \ref StringStyle.
struct StringStyle;
[[nodiscard]] PlatePalette platePalette(const StringStyle& style, Hand hand);

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
// Clear pixels between the closing bracket bar and a displaced posture digit, and the padding its
// ground keeps around a centred one. One pixel binds the digit to its bracket by proximity without
// letting the glyph's antialiasing merge into the bar the way touching it does.
constexpr int g_arpeggio_posture_gap{1};

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

// The plate rect the mute number-plate and the editor's pending entry box share: sized against
// the text's own ink so the box reads as the number's ground, never a fixed chip. One authority
// on purpose — the pending box exists to carry a provisional value in exactly the committed
// number's frame, so a second derivation here would be the drift the export exists to prevent.
[[nodiscard]] juce::Rectangle<float> headTextPlate(
    const TabLaneMetrics& metrics, const juce::String& text, const float center_x,
    const float center_y)
{
    const auto text_width = static_cast<float>(textWidth(metrics.fret_font, text));
    return juce::Rectangle<float>{
        center_x - text_width / 2.0f - 2.0f,
        center_y - metrics.fret_font.getHeight() / 2.0f - 1.0f,
        text_width + 4.0f,
        metrics.fret_font.getHeight() + 2.0f
    };
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

// Every ink one note can be drawn with, per-string and neutral alike.
//
// ONE authority. The per-string half of this list is the Charter derivation chain; the neutral
// half is the greys and whites the technique marks were reaching for directly, from the constants
// above. Both halves were always a note's ink — they were simply held in two places, so anything
// that had to act on ALL of a note's ink (the emphasis axis is the first, and it will not be the
// last) had no single place to act. Naming them one set is what makes StringStyle::ghosted
// possible without a factor threaded through every drawing helper.
enum class Ink : std::uint8_t
{
    Lane,        // string line: base x0.8
    BorderInner, // note ring: lane brightened
    Inner,       // note fill: ring darkened twice
    LinkedInner, // linked-note fill: the fill darkened twice more
    Tail,        // sustain fill: the linked-note fill (see the constructor)
    TailEdge,    // sustain border: the authority's bright x0.66 tail, brightened
    Accent,      // accent glow: ring brightened, hue-preserving

    Digit,         // fret numbers, on the head and on the floating chips
    TechniqueLine, // slide diagonals and the bend polyline
    VibratoSine,   // the sine riding a tail
    MuteBorder,    // the mute X's outline, and every mute number-plate's rim
    PalmMuteInner, // the palm mute X's fill
    PlateRim,      // every letter plate's rim, both hands
    PlateDark,     // the picking hand's plate fill and the fretting hand's letter ink
    PlateLight,    // the fretting hand's plate fill and the picking hand's letter ink

    Count
};

// A 2D ghost keeps the normal note's colors. Its finished note art is flattened before this weight
// is applied, so the head covers its own tail. Fret plates then land at a middle weight while their
// glyphs remain opaque.
constexpr float g_ghost_opacity{0.5f};
constexpr float g_ghost_fret_plate_opacity{0.75f};

// Opens a JUCE transparency layer and closes it after every nested graphics state has unwound.
// JUCE has no RAII form, while ending a layer before an inner ScopedSaveState is destroyed silently
// corrupts the composite. The saved outer state also normalizes Direct2D, which does not restore
// the graphics state on endTransparencyLayer as JUCE's API contract specifies.
class ScopedTransparencyLayer
{
public:
    ScopedTransparencyLayer(
        juce::Graphics& graphics, const juce::Rectangle<int> group_bounds, const float opacity)
        : m_graphics(graphics)
        , m_state(graphics)
    {
        m_graphics.reduceClipRegion(group_bounds);
        m_graphics.beginTransparencyLayer(opacity);
    }

    ScopedTransparencyLayer(const ScopedTransparencyLayer&) = delete;
    ScopedTransparencyLayer(ScopedTransparencyLayer&&) = delete;
    ScopedTransparencyLayer& operator=(const ScopedTransparencyLayer&) = delete;
    ScopedTransparencyLayer& operator=(ScopedTransparencyLayer&&) = delete;

    ~ScopedTransparencyLayer()
    {
        m_graphics.endTransparencyLayer();
    }

private:
    juce::Graphics& m_graphics;
    // Restores the saved outer state after the destructor body closes the layer.
    juce::Graphics::ScopedSaveState m_state;
};

// Bridges the shared Charter-exact style derivation to JUCE colors at this module's boundary;
// the per-string entries match common::ui::StringLaneStyle one for one.
struct StringStyle
{
    std::array<juce::Colour, static_cast<std::size_t>(Ink::Count)> inks{};

    [[nodiscard]] juce::Colour operator[](const Ink ink) const
    {
        return inks.at(static_cast<std::size_t>(ink));
    }

    explicit StringStyle(juce::Colour base)
        : StringStyle(StringLaneStyle{base.getARGB()})
    {}

    // The one deliberate divergence from the authority: the 2D tail FILL is the linked-note fill —
    // as dark as the waypoint heads riding it — not the authority's bright base x0.66. Both
    // surfaces subdue tails, each through its own compositing model: the highway keeps the bright
    // tail and applies translucency over its dark world, while this opaque lane darkens the fill
    // outright. At the bright fill everything drawn ON a tail fought it (the vibrato sine measured
    // barely 6 dL* on the yellow string, whose tail EDGE was lighter than the grey ink crossing
    // it). The edge keeps the authority's derivation untouched: it carries string identity and
    // says the note rings, and keeping it bright is what lets the fill go this dark.
    explicit StringStyle(const StringLaneStyle& style)
    {
        // Assigned by enumerator, never by position: a positional list agreed with the enum only
        // by hand, and reordering either would have swapped two inks without a word from the
        // compiler — the omission check below sees a hole, not a swap.
        set(Ink::Lane, juce::Colour{style.lane});
        set(Ink::BorderInner, juce::Colour{style.border_inner});
        set(Ink::Inner, juce::Colour{style.inner});
        set(Ink::LinkedInner, juce::Colour{style.linked_inner});
        set(Ink::Tail, juce::Colour{style.linked_inner});
        set(Ink::TailEdge, juce::Colour{style.tail_edge});
        set(Ink::Accent, juce::Colour{style.accent});
        set(Ink::Digit, juce::Colours::white);
        set(Ink::TechniqueLine, juce::Colours::white);
        set(Ink::VibratoSine, g_vibrato_sine_color);
        set(Ink::MuteBorder, g_mute_border_color);
        set(Ink::PalmMuteInner, g_palm_mute_inner_color);
        set(Ink::PlateRim, g_plate_rim);
        set(Ink::PlateDark, juce::Colours::black);
        set(Ink::PlateLight, juce::Colours::white);
        // An ink never assigned stays default-constructed — fully transparent — and would then
        // draw NOTHING, silently, wherever it was used. No real ink is transparent, so
        // transparency is a sound sentinel for "never filled in", and this turns the one hazard
        // of an array-shaped palette into a debug failure instead of a mark that vanishes.
        assert(
            std::ranges::none_of(inks, [](const juce::Colour ink) { return ink.isTransparent(); }));
    }

private:
    void set(const Ink ink, const juce::Colour colour)
    {
        inks.at(static_cast<std::size_t>(ink)) = colour;
    }
};

PlatePalette platePalette(const StringStyle& style, const Hand hand)
{
    return hand == Hand::Picking
               ? PlatePalette{.fill = style[Ink::PlateDark], .ink = style[Ink::PlateLight]}
               : PlatePalette{.fill = style[Ink::PlateLight], .ink = style[Ink::PlateDark]};
}

// A mute's fret-number plate, by the SAME rule the letter plates use above: the plate takes a
// mark's fill and the digit takes the contrasting ink, so the plate reads as the X's centre rather
// than as a hole punched through it. SIGNED 2026-08-18 - the full mute's plate had been a mid-gray
// box under a light digit, which the user read as harder to see than the palm mute's, and the fix
// is one rule with two instantiations rather than a second hand-tuned pair.
//
// Keyed on the PALM hand rather than on the full mute, which is what lets ONE rule say all three
// states once the format carries the two mutes independently. The plate-flip design, chosen
// 2026-08-18 from eleven measured candidates at 46.7 dL* of glance separation (today's surfaces
// say nothing at all: 0.0):
//
//   THE X'S FILL SAYS WHAT THE NOTE SOUNDS AS; THE PLATE'S FILL SAYS WHETHER THE PALM HAND IS ON
//   THE STRINGS.
//
// Read through this atlas's own hand signature - dark interior means the picking hand, light means
// the fretting hand - that is not a colour code to memorise but the same rule extended: the DARK
// INK MEANS THE PALM HAND in every state, and it simply moves to the plate when the X is busy
// saying "this sounds dead". A both-muted note therefore wears a full mute's white X over a
// near-black plate, and its residual likeness to a plain full mute is FREE, because the two sound
// and score identically (user ruling 2026-08-18) - the design parks its one ambiguity where it
// costs nothing.
//
// AMENDED 2026-08-19: the X now ALSO carries a near-black rim when both mutes are set
// (drawMuteIcon), which this paragraph used to argue against - reinforcing the pair that way
// measured WORSE, because it drags "both" back toward "palm only", and those two differ in pitch
// where "both" and "dead only" do not. Two separate rounds measured that, and the user sighted the
// rim against the alternatives anyway and chose it. The measurement is not withdrawn and is left
// standing above, because it states the cost the choice accepts: the rim buys a both-muted note a
// visible statement of the palm hand ON THE MARK ITSELF, at the price of moving it a little nearer
// the palm-only reading. The plate rule below is untouched and still carries the fact on its own,
// so nothing depends on the rim being read.
PlatePalette mutePlatePalette(const StringStyle& style, const bool palm_mute)
{
    return palm_mute ? PlatePalette{.fill = style[Ink::PalmMuteInner], .ink = style[Ink::Digit]}
                     : PlatePalette{.fill = style[Ink::PlateLight], .ink = style[Ink::PlateDark]};
}

// Every per-string style one paint can need. Ghosting is a finished-note composite rather than a
// second palette, so normal and ghost notes deliberately read the same opaque colors here.
struct LaneStyles
{
    std::vector<StringStyle> strings;

    // Clamped like the palette itself, which cycles defensively past its tiers: a string outside
    // the chart's range is already drawn off the lane band by laneY, so it wants a color here, not
    // a branch.
    [[nodiscard]] const StringStyle& operator()(const int chart_string) const
    {
        const auto index = static_cast<std::size_t>(
            std::clamp(chart_string, 1, common::core::g_max_chart_strings) - 1);
        return strings[index];
    }
};

[[nodiscard]] LaneStyles makeLaneStyles(const TabLaneMetrics& metrics)
{
    LaneStyles styles;
    styles.strings.reserve(static_cast<std::size_t>(common::core::g_max_chart_strings));
    for (int chart_string = 1; chart_string <= common::core::g_max_chart_strings; ++chart_string)
    {
        styles.strings.emplace_back(metrics.baseColor(chart_string));
    }
    return styles;
}

// A floating label chip collected during the tail passes and drawn above every note head.
struct LabelChip
{
    juce::Point<float> position;
    juce::String text;
    juce::Colour background;
    juce::Colour border;

    // Chips are collected during the tail pass and drawn LAST, above every head. Their box follows
    // the resolved opacity; fret-label ink can opt out while bend amounts remain part of the group.
    juce::Colour ink;
    float opacity;
    bool opaque_ink;
};

// The note head sounding on this string exactly at the span start, or nullptr when the string is
// silent there. That head is what the centred posture digit contends with; its ATTACK decides
// which hand produced it and its FRET whether the posture is already stated.
//
// Reads the state's own notes rather than the drawn-note accessor, and is not a site that missed
// it: everything asked here — onset, string, attack, fret — is what presentation leaves untouched,
// so both forms of a note answer identically.
[[nodiscard]] const common::core::NoteViewState* headAtSpanStart(
    const common::core::ChartViewState& tab, double span_start_seconds, int chart_string)
{
    // The same question every same-instant test on either surface asks — are these two chart
    // times one moment — so it reads the one named tolerance rather than restating the number.
    constexpr double tolerance = common::core::g_onset_match_epsilon;
    const auto onset = &common::core::NoteViewState::start_seconds;
    for (auto it = std::ranges::lower_bound(
             tab.notes, span_start_seconds - tolerance, std::ranges::less{}, onset);
         it != tab.notes.end() && it->start_seconds <= span_start_seconds + tolerance;
         ++it)
    {
        if (it->string == chart_string)
        {
            return &*it;
        }
    }
    return nullptr;
}

struct ArpeggioBracket
{
    common::core::ShapeStringViewState note;
    // The span start in pixels: the bracket pair's center, and the head's own column.
    float center_x{};
    // The resolved digit box, plus the fact that placed it: whether this string's posture was
    // displaced into the side slot by a tap. A centred digit needs no ground of its own — the
    // technique marks that would cross it clip against the bracket's columns at the source.
    int digit_left{};
    int digit_width{};
    bool side_slot{};
    // The bracket bars' own pixel columns.
    int bar_left{};
    int bar_right{};
    // Right edge of the WHOLE mark: the bars plus the posture digit sitting outboard of the
    // closing bar. The lane line is gapped from bar_left to here rather than to bar_right, so the
    // digit reads on clean background exactly like the bracketed head does. Equals bar_right when
    // the lane is too short to carry text at all.
    int mark_right{};
};

// Draws one string line per displayed lane across the given clip (the repaint region held to the
// lane bounds), exactly like Charter's lane lines: one pixel, the string's base color at 80%. Each
// lane's line leaves a gap over every arpeggio bracket on it, so the "[ fret ]" posture marks sit
// on a clean background instead of the line cutting through them. Brackets arrive in ascending
// span order, which is what lets one left-to-right cursor walk per lane cover them.
void drawStringLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, juce::Rectangle<int> clip,
    const std::vector<ArpeggioBracket>& brackets)
{
    for (int displayed_string = 1; displayed_string <= metrics.displayed_count; ++displayed_string)
    {
        const float y = tabLaneCenterY(displayed_string, metrics.displayed_count, metrics.bounds);
        g.setColour(
            charterMultiply(tabStringColor(displayed_string, metrics.displayed_count), 0.8));
        // The lane centre IS a row centre (tabLaneCenterY snaps it), so the row this line fills
        // is that centre less half a row. This used to snap independently with `(int)y`, which
        // gave the same answer and was therefore a second authority on the same fact — the kind
        // that only shows up when one of them moves.
        const float row = y - 0.5f;
        auto cursor = static_cast<float>(clip.getX());
        const auto right = static_cast<float>(clip.getRight());
        for (const ArpeggioBracket& bracket : brackets)
        {
            if (common::core::displayedLane(bracket.note.string, metrics.extra_lanes) !=
                displayed_string)
            {
                continue;
            }

            const float gap_start =
                std::min(right, std::max(cursor, static_cast<float>(bracket.bar_left)));
            if (gap_start > cursor)
            {
                g.fillRect(juce::Rectangle<float>{cursor, row, gap_start - cursor, 1.0f});
            }
            cursor = std::max(cursor, static_cast<float>(bracket.mark_right));
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

// The stretch of a tail that has to be generated, as distances from the onset. Both wavy tail
// overlays are functions of the distance from the onset alone, so each can be generated across
// just this stretch and land the identical shape — phase comes from the distance, never from
// where generation began.
struct TailRun
{
    float from_dx{};
    float to_dx{};

    [[nodiscard]] bool empty() const noexcept
    {
        return to_dx <= from_dx;
    }
};

// The part of a tail the clip can actually show. A tail is as long as its note, which at full
// zoom is many screens wide, so generating one vertex per pixel (or per tremolo apex) of the
// whole length spends nearly all of it off-screen: a held tremolo chord costs tens of thousands
// of invisible vertices every frame. Clamped to the tail, so a run never reports geometry past
// the note's own end.
//
// Overshoots the clip, which is what keeps a narrowed repaint from showing a seam at its own
// edge: a generated run ENDS where the whole-length one merely passed through, and an end differs
// from a pass-through — a filled band closes on a vertical edge there, a stroked overlay caps
// instead of joining. The overshoot has to exceed how far back that difference reaches, which is
// half the widest stroke an overlay wears (the vibrato sine's, tail_height/8) plus the one-pixel
// sample step; a quarter of the tail height clears that at every lane scale. Each generator then
// snaps the run outward onto its own vertex spacing, so the vertices inside the clip are exactly
// the ones the whole-length geometry would have placed and the rasterized result is identical
// rather than merely similar.
[[nodiscard]] TailRun visibleTailRun(
    const juce::Graphics& g, const TabLaneMetrics& metrics, float onset_x, float length)
{
    const float slack = std::max(2.0f, metrics.tail_height / 4.0f);
    const juce::Rectangle<float> clip = g.getClipBounds().toFloat();
    return TailRun{
        .from_dx = std::clamp(clip.getX() - slack - onset_x, 0.0f, length),
        .to_dx = std::clamp(clip.getRight() + slack - onset_x, 0.0f, length),
    };
}

// The thickness of a head's bright ring. VARIANT: with the dark backings gone this has one caller
// again (fillHeadShape), where the size/15 came from; it stays a named function so the committed
// version is a one-line diff away rather than a re-inline.
[[nodiscard]] float noteBorderThickness(const float head_size)
{
    return std::max(1.0f, head_size / 15.0f);
}

// The sustain tail's shape, as the centreline its two rails are laid either side of. ONE authority
// for the ribbon and for the accent halo that traces it: a plain tail is a flat two-point line, a
// tremolo band is one point per apex, and nothing downstream has to know which it is. Sharing it is
// the whole point — the halo used to be two straight fillRects while the band snaked, so the two
// disagreed about the same edge and left up to 3.15 px of bare lane opening and closing every
// 6.25 px along the tail.
struct TailCenterline
{
    // Centreline points, left to right along the tail.
    std::vector<juce::Point<float>> points;

    // Half the band's thickness; each rail lies this far off the centreline.
    float half_thickness{};
};

// The tremolo band's centreline: a triangle wave, zero at the onset so the band leaves the head
// centered, which puts apex n on the half-odd multiple (n + 1/2) of the step.
[[nodiscard]] TailCenterline tremoloCenterline(
    juce::Graphics& g, const TabLaneMetrics& metrics, const float x, const float length,
    const float center_y)
{
    const TailRun run = visibleTailRun(g, metrics, x, length);
    if (run.empty())
    {
        return TailCenterline{};
    }

    const TailSpan span = tailSpan(metrics, center_y);
    const float band_center = (span.top + span.bottom) / 2.0f;
    // Half the tremolo size each way: the swing the band adds to its thickness is the swing it
    // takes back by snaking, so the envelope is the plain span plus the whole tremolo size and the
    // core is the plain span exactly.
    const float amplitude = metrics.tremolo_size / 2.0f;
    const float apex_step = std::max(2.0f, metrics.note_height / 4.0f);

    const auto centerline_at = [&](const float dx) {
        const float cycles = (dx / (2.0f * apex_step)) - 0.25f;
        const float phase = cycles - std::floor(cycles);
        return band_center + (amplitude * (std::abs(phase - 0.5f) - 0.25f) * 4.0f);
    };
    const auto apex_dx = [&](const int apex) {
        return (static_cast<float>(apex) + 0.5f) * apex_step;
    };
    // The band's ends: the visible run widened outward to the apexes bracketing it, then held to
    // the tail itself. Every vertex is therefore one the whole-length band would also have placed
    // — an apex, the onset, or the tail's end — which is what makes a narrowed repaint rasterize
    // identically. Wherever the run stops, the band closes on a clean vertical edge with no tooth
    // left half-drawn, because centerline_at is exact and the wave is straight between apexes.
    const int first_apex = static_cast<int>(std::floor((run.from_dx / apex_step) - 0.5f));
    const int last_apex = static_cast<int>(std::ceil((run.to_dx / apex_step) - 0.5f));
    const float from_dx = std::max(0.0f, apex_dx(first_apex));
    const float to_dx = std::min(length, apex_dx(last_apex));
    const int inner_first = std::max(first_apex + 1, 0);
    const int inner_last = last_apex - 1;
    const int vertex_count = std::max(0, inner_last - inner_first + 1) + 2;

    TailCenterline centerline;
    centerline.half_thickness = ((span.bottom - span.top) / 2.0f) + amplitude;
    centerline.points.reserve(static_cast<std::size_t>(vertex_count));
    for (int index = 0; index < vertex_count; ++index)
    {
        const float dx = index == 0                  ? from_dx
                         : index == vertex_count - 1 ? to_dx
                                                     : apex_dx(inner_first + index - 1);
        centerline.points.emplace_back(x + dx, centerline_at(dx));
    }
    return centerline;
}

// A plain sustain's centreline: the tail span's own middle, straight from the onset to the tail's
// end. It is the DEGENERATE tremolo band — one segment, no swing — which is what lets the ribbon
// and the halo take one rule each instead of one per tail kind. Every downstream expression
// collapses to the straight-band form on it: the halo's per-segment gradient becomes the vertical
// gradient a plain tail has always drawn, and its quad becomes the same rectangle.
[[nodiscard]] TailCenterline plainCenterline(
    const TabLaneMetrics& metrics, const float onset_x, const float end_x, const float center_y)
{
    const TailSpan span = tailSpan(metrics, center_y);
    const float middle = (span.top + span.bottom) / 2.0f;
    return TailCenterline{
        .points = {{onset_x, middle}, {end_x, middle}},
        .half_thickness = (span.bottom - span.top) / 2.0f,
    };
}

// Draws the sustain tail as a constant-thickness zigzag band: the plain sustain's ribbon with its
// top and bottom borders displaced TOGETHER, so the strip snakes instead of pulsing in thickness
// the way the ported pointed-gem chain did. This matches the 3D highway's teeth, which swing a
// constant-width ribbon the same way. Drawn edge-colored with the tail color inset by the edge
// size, like every other tail.
//
// The band is the plain tail's span grown by half the tremolo size on each side and swung by that
// same half (see tremoloCenterline), which pins two things at once: the outer envelope stays
// exactly the gem chain's — the tail occupies the same rows it always has — and the strip's
// ALWAYS-covered core is exactly the plain span, so a slide diagonal, which is drawn to that span,
// sits entirely inside the band at every x instead of crossing its teeth. Apexes come twice per
// gem cell, double the chain's rate, which reads as picking rather than as a slow wave.
void drawTremoloTail(
    juce::Graphics& g, const StringStyle& style, const TabLaneMetrics& metrics,
    const TailCenterline& centerline)
{
    if (centerline.points.size() < 2)
    {
        return;
    }

    const auto add_band = [&](juce::Path& path, const float inset) {
        const float half = std::max(1.0f, centerline.half_thickness - inset);
        const std::vector<juce::Point<float>>& points = centerline.points;
        path.startNewSubPath(points.front().x, points.front().y - half);
        for (std::size_t index = 1; index < points.size(); ++index)
        {
            path.lineTo(points[index].x, points[index].y - half);
        }
        for (std::size_t index = points.size(); index-- > 0;)
        {
            path.lineTo(points[index].x, points[index].y + half);
        }
        path.closeSubPath();
    };

    juce::Path edge_band;
    add_band(edge_band, 0.0f);
    g.setColour(style[Ink::TailEdge]);
    g.fillPath(edge_band);

    juce::Path inner_band;
    add_band(inner_band, metrics.tail_edge_size);
    g.setColour(style[Ink::Tail]);
    g.fillPath(inner_band);
}

// How far past its subject's edge an accent's halo reaches, as a fraction of the note height. ONE
// number for both halos: the head's glow ellipse is its head grown by this on every side, and the
// tail's halo takes the same absolute distance, so one accent reads at one strength wherever on
// the note it appears rather than the two drifting apart under separate literals.
constexpr float g_accent_glow_reach_heads = 0.2f;

// Draws the accent's glow behind the sustain tail: two bands riding the tail's rails, fading
// outward, with NO cap at either end.
//
// An accent reaches the TAIL because it is a dynamic marking and not an attack-only one. Notation
// defines the mark as "a louder dynamic AND a stronger attack", and the physics says the same
// thing less ambiguously: a plucked string rings as A * exp(-lambda * t), where picking harder
// raises the initial amplitude A while lambda is fixed by the damping rather than by the
// excitation — so an accented note is louder at EVERY instant of its ring, not only at its onset.
// The intuition that an accent is "an attack thing" comes from the GLYPH, a point symbol sitting
// over the head; but this surface renders the accent as light rather than as a glyph, and once the
// phenomenon is what is drawn, the phenomenon's extent governs.
//
// The quiet end of this axis already reached the tail here (a ghost fades the whole ink set, the
// ribbon with it), so a head-only accent left the axis saying different things at its two ends on
// one surface. The highway reached this same conclusion for its ribbon; this is the 2D half.
//
// NO END CAP. The tail itself draws top and bottom rails only — the left end omitted because the
// head covers it, the right end because a cap boxes in whatever technique mark reaches the tail's
// tip (SIGNED 2026-08-16 with the bare end chosen over both a cap and a dissolve). A glow wrapping
// the tip would restore that cap in light and box the mark in exactly the same way, so the halo
// ends where the rails end and states nothing about the tip that the ribbon does not.
//
// This is also why the halo does not fade ALONG the tail as the highway's does. Both surfaces
// obey one rule — the accent light traces the tail that surface actually draws — and they differ
// only because the ribbons do: the highway's light fades because its ribbon's alpha fades, while
// the editor's ribbon is uniform with a hard stop, so its halo is uniform and stops with it.
//
// It traces the CENTRELINE it is handed rather than a pair of straight lines, which is what makes
// it correct on a tremolo band. A straight halo against a snaking ribbon left 0.0127 to 3.1540 px
// of bare lane, opening and closing every 6.25 px — 224 bare-lane pixels on one tail, where a
// plain tail has 0. Tracing brings that to 0.0000 px. A plain tail hands in a flat two-point
// centreline and every expression below collapses to the straight-band form it had before.
void drawAccentTailGlow(
    juce::Graphics& g, const StringStyle& style, const TailCenterline& centerline,
    const float reach)
{
    const std::vector<juce::Point<float>>& points = centerline.points;
    if (points.size() < 2)
    {
        return;
    }

    for (const float outward : {-1.0f, 1.0f})
    {
        for (std::size_t index = 0; index + 1 < points.size(); ++index)
        {
            const juce::Point<float> edge_from{
                points[index].x, points[index].y + (outward * centerline.half_thickness)
            };
            const juce::Point<float> edge_to{
                points[index + 1].x, points[index + 1].y + (outward * centerline.half_thickness)
            };
            const float run_x = edge_to.x - edge_from.x;
            const float run_y = edge_to.y - edge_from.y;
            const float run_squared = (run_x * run_x) + (run_y * run_y);
            if (!(run_squared > 0.0f))
            {
                continue;
            }
            // TWO DIFFERENT AXES, and keeping them apart is the whole correctness of this
            // function.
            //
            // The QUAD is the edge segment extruded VERTICALLY by the full reach. It has to be
            // vertical because the tail's every other thickness is: `half_thickness` is a
            // vertical half-thickness, tailSpan is a vertical span, and a plain tail's halo was a
            // vertical fillRect. Extruding the quad perpendicularly instead shortens it to
            // reach * run_x^2 / |run|^2 (4.138 px of the 5.200 at the shipped lane, a fifth of the
            // halo gone) and, worse, slides its outer corners sideways by
            // reach * run_x * run_y / |run|^2, so consecutive quads' outer corners land 4.193 px
            // apart in x: a bare wedge at every apex that turns one way and a double-blended
            // overlap at every apex that turns the other. Vertical extrusion has neither, because
            // the outer boundary is then the centreline's own polyline translated, and a
            // translated polyline still meets itself at every vertex.
            //
            // The GRADIENT's axis is the perpendicular one, and only the gradient's. Its
            // iso-alpha lines have to run PARALLEL to the edge or the ramp would fade along the
            // tail instead of across it, so its far point is the edge point pushed along the
            // segment normal by exactly as far as a vertical reach carries: |scale| * |run|.
            // Alpha at any point is then 1 - (vertical distance outward) / reach, and with
            // run_y == 0 the whole expression collapses to the straight-band gradient a plain
            // tail has always drawn.
            //
            // The colour order is that straight case's - clear at the outer point, accent ON the
            // edge - and it has to stay that way. JUCE FLOORS the gradient's lookup index, so
            // running the ramp the other way shifts every sample a whole table step: 19 counts of
            // alpha on a flat edge, exactly where this and the straight case must agree.
            const float scale = -outward * reach * run_x / run_squared;
            const juce::Point<float> gradient_end{
                edge_from.x + (scale * run_y), edge_from.y - (scale * run_x)
            };
            const float rise = outward * reach;

            juce::Path quad;
            quad.startNewSubPath(edge_from);
            quad.lineTo(edge_to);
            quad.lineTo(edge_to.x, edge_to.y + rise);
            quad.lineTo(edge_from.x, edge_from.y + rise);
            quad.closeSubPath();

            g.setGradientFill(
                juce::ColourGradient{
                    style[Ink::Accent].withAlpha(0.0f),
                    gradient_end,
                    style[Ink::Accent],
                    edge_from,
                    false
                });
            g.fillPath(quad);
        }
    }
}

// Draws the sustain tail's BODY: Charter's filled bar with its brighter rails, or the tremolo gem
// strip variant. The vibrato sine is drawn separately (drawVibratoSine) because it is a technique
// mark riding the tail, clipped against arpeggio brackets where the body is not.
void drawNoteTail(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float onset_x, float center_y)
{
    // The PRESENTED tail and nothing else, so a note that presents none draws none — including a
    // chugged member of a strum a hand-shape span holds, which the span-implied hold
    // (ChartViewState::display_hold_ends) does extend on the 3D board. This lane says the same
    // thing in its own idiom: the chord box over the strum already states how long the posture is
    // fretted, and a ribbon under every chug restated it in the one mark that means "this string
    // is still ringing".
    const float end_x = metrics.x(note.end_seconds);
    const float length = end_x - onset_x;
    if (length <= 0.0f)
    {
        return;
    }

    const TailSpan span = tailSpan(metrics, center_y);
    // The tail's shape, resolved ONCE and handed to both the ribbon and its halo. This is what
    // deleted the old `swing` constant: the centreline carries the band's full excursion, so
    // nothing has to restate how far past the plain span a tremolo reaches — a question the
    // straight-halo version got wrong twice, first by half (reading one amplitude where the band
    // swings two) and then in kind (a straight line against a snaking edge).
    const TailCenterline centerline = note.tremolo
                                          ? tremoloCenterline(g, metrics, onset_x, length, center_y)
                                          : plainCenterline(metrics, onset_x, end_x, center_y);
    // Measured from the RAIL, which is the tail's outermost ink now that the dark backing is gone
    // (sighted 2026-08-19): the light leaves the bright edge directly, the way a real emitter does
    // rather than across a dark gap. The head obeys the same law — its glow stands off the bright
    // ring, not the empty margin outside it — so one accent reads at one strength across the note.
    if (common::core::isAccented(note.emphasis))
    {
        drawAccentTailGlow(g, style, centerline, metrics.headSize() * g_accent_glow_reach_heads);
    }
    // The teeth mean REPEATED ATTACKS, so only `tremolo` wears them. A scrape is one continuous
    // drag — teeth would assert a repetition it never performs, and it cannot be tremolo picked
    // at all (E2) — so it draws the plain ribbon with its slide diagonals carrying the travel,
    // and its plectrum head states that the noise is unpitched. That division is the rule for
    // the whole tail axis: the head says what kind of attack, the tail says what happens over
    // time (ribbon = duration, diagonal = travel, curve = bend, sine = vibrato, teeth =
    // repetition). It is also what makes a muted tremolo slide — noisy travel — read apart from
    // a plain muted slide's single drag.
    if (note.tremolo)
    {
        drawTremoloTail(g, style, metrics, centerline);
    }
    else
    {
        const float thickness = metrics.tail_edge_size;
        const auto fill = [&](const juce::Colour colour, const float top, const float height) {
            g.setColour(colour);
            g.fillRect(juce::Rectangle<float>{onset_x - 1.0f, top, end_x - onset_x + 1.0f, height});
        };

        // The fill covers the whole envelope and the rails lay over its top and bottom — every
        // color here is opaque, so painting the rails over the fill is the same pixels as
        // abutting them, without the two rectangles having to agree on a seam.
        fill(style[Ink::Tail], span.top, span.bottom - span.top);
        // TOP AND BOTTOM RAILS ONLY — no cap on either end. The left edge is omitted because the
        // head covers it; the right edge is omitted because a cap boxes in whatever technique mark
        // reaches the tail's end, and the highway draws none.
        //
        // SIGNED 2026-08-16, from three candidates sighted side by side on a toggle:
        //   mark to end (this) - bare ends, every mark running the full ribbon
        //   end cap            - the cap restored, marks inset by a stroke to meet its inner face
        //   fade out           - bare ends with the last stretch dissolving, as the highway does
        // The ruling turns on this being the EDITOR: a charter needs to see exactly where a
        // sustain stops, and a dissolve trades that endpoint away for softness. That reasoning
        // does not transfer to the game's highway, which is why the two surfaces legitimately end
        // a tail differently — the highway's dissolve is not a divergence to be reconciled. The
        // cap was the close second, so if the bare end ever reads as unfinished, restore the cap
        // rather than reaching for the dissolve; both losers are recoverable from git history.
        fill(style[Ink::TailEdge], span.top, thickness);
        fill(style[Ink::TailEdge], span.bottom - thickness, thickness);
    }
}

// The tail's INTERIOR: the band between the two edge rails drawNoteTail lays inside the span's
// envelope, symmetric about the string line like the envelope itself. This is the one definition
// of where a technique mark may live — the sine and the bend polyline COMPRESS their swing to fit
// it, the slide diagonals anchor their endpoints on it, the technique clip holds every mark inside
// it, and the side chip's ground fills exactly it — so a mark meets the tail's edge scaled, never
// cut.
struct TailInterior
{
    float top;
    float bottom;
};

[[nodiscard]] TailInterior tailInterior(const TabLaneMetrics& metrics, const float center_y)
{
    const TailSpan span = tailSpan(metrics, center_y);
    return TailInterior{
        .top = span.top + metrics.tail_edge_size, .bottom = span.bottom - metrics.tail_edge_size
    };
}

// Draws the vibrato sine along a tail. Separate from drawNoteTail because the sine is a technique
// mark RIDING the tail rather than the tail's own body, and the caller clips the technique marks
// against the arpeggio brackets while the ribbon shows through them untouched.
void drawVibratoSine(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float onset_x, float center_y)
{
    const float length = metrics.x(note.end_seconds) - onset_x;
    if (!note.vibrato || length <= 0.0f)
    {
        return;
    }

    // The swing COMPRESSES to the tail's interior, stroke included, rather than being clipped
    // against it: a truncated crest reads as a drawing error where a scaled one still reads as
    // vibrato. Only the amplitude compresses; the period keeps its tail-height derivation so the
    // wave's pacing never changes with the squeeze.
    const float stroke = std::max(1.0f, metrics.tail_height / 8.0f);
    const TailInterior interior = tailInterior(metrics, center_y);
    const float interior_center = (interior.top + interior.bottom) / 2.0f;
    const float amplitude =
        std::max(1.0f, ((interior.bottom - interior.top) / 2.0f) - (stroke / 2.0f));
    const float period = metrics.tail_height;
    // Sampled on whole-pixel distances from the onset, so the run the clip can show carries
    // the same vertices at the same places the whole tail would have put there.
    const TailRun run = visibleTailRun(g, metrics, onset_x, length);
    if (run.empty())
    {
        return;
    }
    const auto first_step = static_cast<int>(std::ceil(run.from_dx));
    const auto last_step = static_cast<int>(std::floor(run.to_dx));
    juce::Path wave;
    for (int step = first_step; step <= last_step; ++step)
    {
        const auto dx = static_cast<float>(step);
        const juce::Point<float> point{
            onset_x + dx,
            interior_center + amplitude * std::sin(dx * juce::MathConstants<float>::twoPi / period)
        };
        if (step == first_step)
        {
            wave.startNewSubPath(point);
        }
        else
        {
            wave.lineTo(point);
        }
    }
    g.setColour(style[Ink::VibratoSine]);
    g.strokePath(wave, juce::PathStrokeType{stroke});
}

// The note-head silhouettes. The shape carries what KIND of note this is; it never carries which
// hand produced it, which is what a present mark's DARKNESS says instead. So the plectrum names a
// pick scrape while the head itself keeps the ordinary string colors, and only the beside-head
// chip goes dark.
enum class HeadShape : std::uint8_t
{
    Round,
    Diamond,
    Plectrum
};

// Picks the silhouette naming this note's kind. The diamond names a head that SOUNDS at a node —
// the same sounding rule the highway's node head asks (highwayNodeHead) and the head text below
// labels by, so the shape and the label can never disagree. A pinch's node lies off the neck where
// the thumb grazes, and both surfaces today draw only a pinch's fretted stop, so it wears the
// ordinary head; how the right-hand node will be shown is an open question. The diamond takes
// precedence over the scrape's plectrum only so the mapping is total: no note can ask for both,
// since a pinch and a scrape are two values of one attack and the chart rules reject a scrape
// carrying a node.
[[nodiscard]] HeadShape headShapeFor(const common::core::NoteViewState& note)
{
    if (common::core::soundingPositionAt(note.harmonic_node, note.attack, note.fret, note.fret)
            .at_node)
    {
        return HeadShape::Diamond;
    }
    if (common::core::isScrape(note.attack))
    {
        return HeadShape::Plectrum;
    }
    return HeadShape::Round;
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
//
// Shared by the onset head and a scrape's junction heads: both are plectrums on one gesture, so
// their digits must sit at the same height.
constexpr float g_plectrum_digit_raise = 0.1154f;

// The digit's vertical raise for one head shape: only the plectrum moves its number — the disc
// and the diamond are widest on the string line, so their digits stay centered on it. The one
// authority for every drawer that places a head digit (the onset head, a scrape's junction
// heads, and the pending entry box), so one shape's digit cannot sit at two heights.
[[nodiscard]] constexpr float headDigitRaise(const HeadShape shape, const float size)
{
    return shape == HeadShape::Plectrum ? g_plectrum_digit_raise * size : 0.0f;
}

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
    for (const juce::Point<float>& outline_point : g_plectrum_half_outline | std::views::drop(1))
    {
        shape.lineTo(vertex(outline_point, 1.0f));
    }
    // Back up the mirrored side, skipping the tip the two halves share.
    for (const juce::Point<float>& outline_point :
         g_plectrum_half_outline | std::views::reverse | std::views::drop(1))
    {
        shape.lineTo(vertex(outline_point, -1.0f));
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
    const float border = noteBorderThickness(size);

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

    // Two layers, and the outermost `border` of the head's box is left EMPTY on purpose. It used
    // to hold a dark backing in the lane's own ground colour, which was invisible over bare lane
    // and did its only visible work where the head overlapped its own tail, separating the two.
    // Sighted 2026-08-19 against four alternatives and dropped: the separation it bought was not
    // worth a dark rim on every note, and the plain head reads cleaner and matches the highway.
    // The empty margin stays because `size` is what every other mark on the head is measured
    // against - the mute X, the plate, the glow - so shrinking the box would move all of them.
    layer(border, border_inner);
    layer(border * 2.0f, inner);
}

// Charter's white technique-line stroke, shared by the slide diagonals and the bend polyline.
// One constant because the interior anchoring assumes it: both drawers inset their endpoints by
// half of THIS stroke, so a divergence would push one of them back onto the rails.
constexpr float g_technique_line_thickness = 2.0f;

// Draws Charter's slide line: a white two-pixel diagonal across the tail toward the target fret,
// rising for ascending slides. Waypoint chains continue segment by segment; unpitched targets
// get Charter's fret label chip (white on the tail color darkened three times) at the segment
// end, exactly as Charter labels unpitched slides.
void drawSlideLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float onset_x, float center_y,
    std::vector<LabelChip>& slide_labels, const float opacity)
{
    if (note.slides.empty())
    {
        return;
    }

    constexpr float line_thickness = g_technique_line_thickness;
    const TailSpan span = tailSpan(metrics, center_y);
    // Diagonals span the tail's INTERIOR, endpoint stroke included: anchored a half-thickness
    // inside the rails' inner boundaries, so the line meets the tail's edge without ever riding
    // onto the rail — a mark reaching the outer edge reads as leaking out of the sustain.
    const TailInterior interior = tailInterior(metrics, center_y);
    float from_x = onset_x + metrics.note_height / 4.0f;
    int previous_fret = note.fret;
    std::size_t leg = 0;
    for (const common::core::SlideViewState& waypoint : note.slides)
    {
        // Every junction insets its endpoint by one stroke width, which opens a hairline gap
        // between consecutive diagonals so a multi-waypoint glide reads as separate legs. The LAST
        // one takes no inset: its inset existed only to meet the tail's end cap, and with the cap
        // gone (see drawNoteTail) it would leave a stub of bare ribbon past the mark's tip rather
        // than separate anything.
        const bool final_leg = ++leg == note.slides.size();
        const float to_x = metrics.x(waypoint.seconds) - (final_leg ? 0.0f : line_thickness);
        // A hold segment (same fret) is a tie, not a glide: no diagonal — the linked head at
        // the waypoint renders the continuation, and the next segment's line leaves from here.
        if (waypoint.fret == previous_fret)
        {
            from_x = to_x;
            continue;
        }
        const bool upward = waypoint.fret >= previous_fret;
        const float from_y =
            upward ? interior.bottom - line_thickness / 2.0f : interior.top + line_thickness / 2.0f;
        const float to_y =
            upward ? interior.top + line_thickness / 2.0f : interior.bottom - line_thickness / 2.0f;

        g.setColour(style[Ink::TechniqueLine]);
        g.drawLine(from_x, from_y, to_x, to_y, line_thickness);

        // A junction that carries a continuation head shows its fret ON the head, so the chip
        // would be the same number twice. Only an unpitched END keeps the chip: a trail-off and a
        // scrape's terminal have no head, because nothing lands where the string is released.
        if (waypoint.unpitched && !common::core::linkedWaypoint(note, waypoint) &&
            metrics.draw_text)
        {
            const float label_y = upward ? span.top - metrics.note_height / 3.0f
                                         : span.bottom + metrics.note_height / 3.0f;
            slide_labels.push_back(
                LabelChip{
                    .position = {metrics.x(waypoint.seconds), label_y},
                    // Through the same head-label rule, not a raw fret: a stopped harmonic labels
                    // NODES everywhere else on the gesture, and one gesture must not state two
                    // different quantities. (A scrape is unaffected — the writer strips its node.)
                    .text = tabNoteHeadText(note, waypoint.fret),
                    .background = charterDarker(charterDarker(charterDarker(style[Ink::Tail]))),
                    .border = style[Ink::Tail],
                    .ink = style[Ink::Digit],
                    .opacity = opacity,
                    .opaque_ink = true,
                });
        }

        from_x = to_x;
        previous_fret = waypoint.fret;
    }
}

// Draws Charter's linked-note head shapes at each linked slide waypoint. Charter charts express
// unpicked slide chains as linked notes and draw one of these at every link; our format merges the
// chain into waypoints, so the linked waypoints are exactly where Charter's linked heads sit.
//
// A scrape's turnarounds are linked too, and they wear the note's OWN head shape — the plectrum —
// so each junction reads as one continuous gesture changing direction rather than a chain of
// disconnected diagonals. Without a head the corner is a bare kink in a white line, which reads
// as discontinuous even though the pick never leaves the string; the head is also where the
// traveled fret is stated, replacing the chip that used to float above the line.
void drawSlideWaypointHeadShape(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const common::core::SlideViewState& waypoint,
    const float center_y)
{
    const float size = metrics.headSize();
    fillHeadShape(
        g,
        style[Ink::BorderInner],
        style[Ink::LinkedInner],
        metrics.x(waypoint.seconds),
        center_y,
        size,
        headShapeFor(note));
}

// Draws the fully opaque fret number that rides one linked slide waypoint head.
void drawSlideWaypointFretNumber(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const common::core::SlideViewState& waypoint,
    const float center_y)
{
    if (!metrics.draw_text)
    {
        return;
    }

    // A junction labels its own stop through the SAME rule the onset head uses, so one gesture
    // cannot show two different quantities: on a harmonic the onset and junction label nodes.
    const juce::String text = tabNoteHeadText(note, waypoint.fret);
    const float size = metrics.headSize();
    const float x = metrics.x(waypoint.seconds);
    const float digit_raise = headDigitRaise(headShapeFor(note), size);
    g.setColour(style[Ink::Digit]);
    g.setFont(metrics.fret_font);
    g.drawText(
        text,
        juce::Rectangle<float>{x - size, center_y - size - digit_raise, size * 2.0f, size * 2.0f},
        juce::Justification::centred);
}

// Draws only the shapes so a ghost can flatten them into its translucent note group.
void drawSlideWaypointHeadShapes(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const float center_y)
{
    for (const common::core::SlideViewState& waypoint : note.slides)
    {
        if (!common::core::linkedWaypoint(note, waypoint))
        {
            continue;
        }

        drawSlideWaypointHeadShape(g, metrics, style, note, waypoint, center_y);
    }
}

// Draws the fully opaque fret numbers that ride linked slide waypoint heads.
void drawSlideWaypointFretNumbers(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float center_y)
{
    for (const common::core::SlideViewState& waypoint : note.slides)
    {
        if (!common::core::linkedWaypoint(note, waypoint))
        {
            continue;
        }

        drawSlideWaypointFretNumber(g, metrics, style, note, waypoint, center_y);
    }
}

// Draws a normal linked waypoint head in its established shape-then-number order.
void drawSlideWaypointHeads(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const float center_y)
{
    for (const common::core::SlideViewState& waypoint : note.slides)
    {
        if (!common::core::linkedWaypoint(note, waypoint))
        {
            continue;
        }

        drawSlideWaypointHeadShape(g, metrics, style, note, waypoint, center_y);
        drawSlideWaypointFretNumber(g, metrics, style, note, waypoint, center_y);
    }
}

// Draws Charter's bend presentation: a white two-pixel polyline stepping between bend heights
// over the tail, then a flat run to the tail end, with a "<slur><amount>" chip at each bend
// point (white text on the string's lane color darkened twice).
void drawBendLines(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float onset_x, float center_y,
    std::vector<LabelChip>& bend_chips, const float opacity)
{
    if (note.bend.empty())
    {
        return;
    }

    // Charter maps bend height across the tail, full at three whole steps — compressed into the
    // tail's INTERIOR with the stroke included, like the vibrato sine, so the polyline meets the
    // rails scaled instead of being cut by the technique clip: rest sits on the interior's floor,
    // three whole steps on its ceiling.
    constexpr float line_thickness = g_technique_line_thickness;
    const TailInterior interior = tailInterior(metrics, center_y);
    const float rest_y = interior.bottom - line_thickness / 2.0f;
    const float full_y = interior.top + line_thickness / 2.0f;
    const auto bend_y = [&](double semitones) {
        const double steps = std::clamp(semitones / 2.0, 0.0, 3.0);
        return rest_y - static_cast<float>(steps / 3.0) * (rest_y - full_y);
    };

    const juce::Colour chip_background = charterDarker(charterDarker(style[Ink::Lane]));
    // The flat run ends where the ribbon it rides does: the note's presented tail, read from the
    // note itself so the polyline cannot outlast the tail under it.
    const float end_x = metrics.x(note.end_seconds);
    juce::Point<float> last{onset_x, bend_y(0.0)};
    g.setColour(style[Ink::TechniqueLine]);
    for (const common::core::BendPointViewState& point : note.bend)
    {
        const juce::Point<float> to{metrics.x(point.seconds), bend_y(point.semitones)};
        g.drawLine(last.x, last.y, to.x, to.y, line_thickness);
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
                    .ink = style[Ink::Digit],
                    .opacity = opacity,
                    .opaque_ink = false,
                });
        }
        last = {to.x + 1.0f, to.y};
    }
    // The held stretch after the last bend point runs all the way to the sustain's end — no inset,
    // for the same reason a slide's final leg takes none: there is no end cap to meet.
    g.drawLine(last.x, last.y, end_x, last.y, line_thickness);
}

// The accent glow's outer diameter: the head's VISIBLE edge grown by the shared reach on every
// side. The visible edge is the bright ring at `size / 2 - border` now that the dark backing is
// gone, and the reach is measured from there — a literal 1.4 * size would expose a further
// `border` of glow inward and the mark would read larger than its reach.
[[nodiscard]] float accentGlowSize(const float size)
{
    return 2.0f *
           (((size / 2.0f) - noteBorderThickness(size)) + (g_accent_glow_reach_heads * size));
}

// Draws Charter's accent glow behind the head: a soft ring fading out just past the head edge.
//
// The plectrum shares the disc's radial fade. An accented scrape is legal — an aggressively
// played pick slide — and the band clears the plectrum's diagonal shoulder by only 0.331 px at
// a 25 px head against the disc's 1.560: visually tight but real. The knob is glow_size, which
// the round head shares, so widening it is a joint retune (measurements in the
// technique-compatibility plan doc).
void drawAccentGlow(
    juce::Graphics& g, const StringStyle& style, float center_x, float center_y, float size,
    HeadShape shape)
{
    const float glow_size = accentGlowSize(size);
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
            g.setColour(style[Ink::Accent].withAlpha(1.0f - 0.25f * static_cast<float>(ring)));
            g.strokePath(outline, juce::PathStrokeType{glow_size * 0.05f});
        }
        return;
    }

    juce::ColourGradient gradient{
        style[Ink::Accent],
        center_x,
        center_y,
        style[Ink::Accent].withAlpha(0.0f),
        center_x,
        center_y + glow_size / 2.0f,
        true
    };
    gradient.addColour(0.8, style[Ink::Accent]);
    gradient.addColour(0.95, style[Ink::Accent].withAlpha(0.0f));
    g.setGradientFill(gradient);
    g.fillEllipse(center_x - glow_size / 2.0f, center_y - glow_size / 2.0f, glow_size, glow_size);
}

// Draws Charter's fat X mute icon over the head, gray-bordered. One X for both mutes: it appears
// whenever either flag is set, and its FILL is keyed on the dead flag alone — white when the note
// sounds dead, near-black when the palm is the only thing damping it. A both-muted note is
// therefore drawn by two elements each reading one flag (the plate below reads the palm hand), and
// no branch anywhere has to know about "both".
void drawMuteIcon(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style, bool palm_mute,
    bool dead, float center_x, float center_y)
{
    if (!common::core::isMuted(palm_mute, dead))
    {
        return;
    }

    // The head's own extent, with no floor of its own. A floor made the X larger than the head it
    // marks at small lane scales and, below about eleven pixels, larger than the lane — painting
    // mute ink onto strings that carry no mute.
    const float size = metrics.headSize();
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

    const juce::Colour inner = dead ? style[Ink::PlateLight] : style[Ink::PalmMuteInner];
    g.setColour(inner);
    g.fillPath(x_shape);
    // E1 (sighting candidate, 2026-08-19): a note carrying BOTH mutes states the fusion on its RIM
    // rather than inside the fill, and states it in the PALM's own near-black rather than the
    // border's grey. That colour is the whole point and the first attempt missed it: the fill says
    // the note sounds DEAD (white) while the rim says the PALM hand is also on the string, so the
    // one mark carries both flags in the two inks that already mean them elsewhere. Doubling a
    // grey rim instead moved the outline by 0.54 px per side and said nothing.
    //
    // The rim is where it goes because the fret plate covers most of the X — 60% at the widest
    // lane this surface draws and 98% at the smallest — so anything stated inside the mark is
    // mostly hidden, while the rim survives at the four exposed tips. The stroke is centred on the
    // outline, so the extra width eats inward as much as outward and the silhouette grows by only
    // half of it.
    const bool both_mutes = palm_mute && dead;
    g.setColour(both_mutes ? style[Ink::PalmMuteInner] : style[Ink::MuteBorder]);
    g.strokePath(
        x_shape, juce::PathStrokeType{std::max(1.0f, space / 3.0f) * (both_mutes ? 2.0f : 1.0f)});
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

// Draws an attack as its standard tab letter on a rounded plate, in the hand's polarity.
//
// The plate's PARALLEL SIDES are the whole point. A triangle tapers, so at the height where a
// capital's ink sits it retains barely half its width — the letters slap and pop used to carry
// overflowed their own badges at every lane size. A square holds the letter at the badge's full
// width, so every lettered attack can share one silhouette and let the letter name the gesture
// and the polarity name the hand, which is what a guitarist reads anyway (T, S, P).
//
// The letter is the fret number's own font, so it is exactly as legible as the digit the reader
// is already reading — no separate size to tune. It draws only when the plate can hold its ink:
// JUCE's font "height" is the ascent-plus-descent line box rather than a cap height (verified
// in juce_Typeface.cpp, getPointsToHeightFactor() = ascent + descent), so a capital's ink is
// only about half the number the font was asked for, and a plate smaller than that ink would
// spill the letter over its edges the way the triangles did.
void drawLetterPlate(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    juce::Rectangle<float> plate, const Hand hand, const juce::String& letters)
{
    const float border = std::max(1.0f, plate.getHeight() / 9.0f);
    const float radius = plate.getHeight() * 0.22f;
    const PlatePalette palette = platePalette(style, hand);

    g.setColour(palette.fill);
    g.fillRoundedRectangle(plate, radius);
    g.setColour(style[Ink::PlateRim]);
    g.drawRoundedRectangle(plate, radius, border);

    const float ink = metrics.fret_font.getHeight() * g_capital_ink_fraction;
    if (metrics.draw_text && plate.getHeight() >= ink + (2.0f * border))
    {
        g.setColour(palette.ink);
        g.setFont(metrics.fret_font);
        g.drawText(letters, plate, juce::Justification::centred);
    }
}

// The square single-capital case the three picking-hand attacks share.
void drawLetterBadge(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style, float center_x,
    float center_y, const Hand hand, const juce::String& letter)
{
    const float side = metrics.note_height * g_letter_badge_fraction;
    drawLetterPlate(
        g,
        metrics,
        style,
        juce::Rectangle<float>{center_x - side / 2.0f, center_y - side / 2.0f, side, side},
        hand,
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

// Draws the attack technique icon beside the head. The vocabulary reads by SHAPE and by FILL:
// the shape says what the gesture IS, the polarity says whose hand performs it. A triangle
// means a resolved connection — the note is not re-attacked — so it needs no letter, only a
// direction (down for a hammer-on, up for a pull-off). Every struck technique wears a lettered
// plate carrying the letter printed tab already uses (T, S, P), one silhouette with the letter
// naming the gesture and the fill naming the hand: the picking hand's marks are dark, the
// fretting hand's light, so the right-hand tap (dark T) and the stated left-hand tap (light T)
// share a letter without colliding. The pick slide takes the same plate in the same slot, just
// two letters wide.
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
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float center_x, float center_y)
{
    // The apex sits at (corner_x - width/2, corner_y) and the corner is tuck from the head's
    // center on both axes, so putting the apex on the rim is solving
    // 2*tuck^2 + width*tuck + width^2/4 - radius^2 = 0.
    const float radius = metrics.headSize() / 2.0f;
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
        case common::core::NoteAttack::Pick:
        case common::core::NoteAttack::Legato:
        {
            // The claim family shares one branch because the mark is the note's RESOLVED motion,
            // never its stored attack: a claim resolves to whichever triangle its predecessor
            // justifies, and a claim nothing justifies draws exactly what the plain pick beside
            // it draws — nothing.
            if (note.legato == common::core::LegatoMotion::Hammer)
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
                    style[Ink::PlateLight],
                    style[Ink::PlateDark]);
            }
            else if (note.legato == common::core::LegatoMotion::Pull)
            {
                drawTriangleIcon(
                    g,
                    metrics,
                    triangle.x,
                    triangle.y,
                    false,
                    style[Ink::PlateLight],
                    style[Ink::PlateDark]);
            }
            break;
        }
        case common::core::NoteAttack::LeftTap:
        {
            // The stated tap wears the tap letter in the FRETTING hand's polarity — the editor's
            // charting mark (ruled 2026-08-11): the motion is a hammer motion, which is why the
            // 3D surfaces draw it merged, but which triangles answer to `H` and which are
            // deliberate statements is information a charter reads constantly, so the lane says
            // it always. The light plate shares the T because it is the same gesture; the
            // polarity names the hand, per the axis above.
            drawLetterBadge(g, metrics, style, badge.x, badge.y, Hand::Fretting, "T");
            break;
        }
        case common::core::NoteAttack::Tap:
        {
            drawLetterBadge(g, metrics, style, badge.x, badge.y, Hand::Picking, "T");
            break;
        }
        case common::core::NoteAttack::Slap:
        {
            drawLetterBadge(g, metrics, style, badge.x, badge.y, Hand::Picking, "S");
            break;
        }
        case common::core::NoteAttack::Pop:
        {
            drawLetterBadge(g, metrics, style, badge.x, badge.y, Hand::Picking, "P");
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
                style,
                juce::Rectangle<float>{
                    chip.x - (width / 2.0f), chip.y - (badge_side / 2.0f), width, badge_side
                },
                Hand::Picking,
                letters);
            break;
        }
        case common::core::NoteAttack::Pinch:
        {
            // A pinch's mark is the bar drawn beside the diamond head with the head itself, not a
            // plate here: it reads as a harmonic cue rather than an attack cue even though the data
            // now lives on the attack.
            break;
        }
    }
}

// Draws the optional mute plate below a fret number at its independently tuned opacity.
void drawNoteHeadFretPlate(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const float onset_x, const float center_y,
    const float opacity)
{
    const bool muted = common::core::isMuted(note.palm_mute, note.dead);
    if (!metrics.draw_text || !muted)
    {
        return;
    }

    const juce::String head_text = tabNoteHeadText(note, note.fret);
    const PlatePalette mute_plate = mutePlatePalette(style, note.palm_mute);
    // Both mutes box the fret number so it stays readable where the X's crossing strokes cut
    // through the digits. The pending entry box shares this exact plate geometry.
    const juce::Rectangle<float> box = headTextPlate(metrics, head_text, onset_x, center_y);
    std::optional<ScopedTransparencyLayer> plate_layer;
    const juce::Rectangle<int> plate_bounds = box.getSmallestIntegerContainer();
    if (opacity < 1.0f && g.clipRegionIntersects(plate_bounds))
    {
        plate_layer.emplace(g, plate_bounds, opacity);
    }
    g.setColour(mute_plate.fill);
    g.fillRect(box);
    g.setColour(style[Ink::MuteBorder]);
    g.drawRect(box, 1.0f);
}

// Draws only the fret-number glyph so a ghost can keep it fully opaque above the faded plate.
void drawNoteHeadFretNumber(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const float onset_x, const float center_y)
{
    if (!metrics.draw_text)
    {
        return;
    }

    const juce::String head_text = tabNoteHeadText(note, note.fret);
    const bool muted = common::core::isMuted(note.palm_mute, note.dead);
    const PlatePalette mute_plate = mutePlatePalette(style, note.palm_mute);
    const HeadShape shape = headShapeFor(note);
    const float size = metrics.headSize();
    const float digit_raise = headDigitRaise(shape, size);
    g.setColour(muted ? mute_plate.ink : style[Ink::Digit]);
    g.setFont(metrics.fret_font);
    g.drawText(
        head_text,
        juce::Rectangle<float>{
            onset_x - size, center_y - size - digit_raise, size * 2.0f, size * 2.0f
        },
        juce::Justification::centred);
}

// Draws the note head art below its fret furniture: accent glow, layered shape, pinch edge and X.
void drawNoteHeadBase(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, float onset_x, float center_y)
{
    const float size = metrics.headSize();
    const HeadShape shape = headShapeFor(note);

    if (common::core::isAccented(note.emphasis))
    {
        drawAccentGlow(g, style, onset_x, center_y, size, shape);
    }

    fillHeadShape(g, style[Ink::BorderInner], style[Ink::Inner], onset_x, center_y, size, shape);

    if (note.attack == common::core::NoteAttack::Pinch)
    {
        // The bar is capped to the DIAMOND's own height and seated ON it (user ruling 2026-08-20):
        // its left edge lands exactly on the diamond's leftmost point, so the bar overlaps the head
        // instead of hanging off its side, and it stands as tall as the diamond rather than as tall
        // as the note. It used to be centred half a note height out from the onset and drawn the
        // full note height, which left it taller than the head it belongs to and mostly outside it.
        //
        // Both numbers come from the head's VISIBLE half-extent — the bright ring at
        // `size/2 - border`, which is the same quantity the accent glow stands off. Asking for it
        // here rather than restating `note_height / 2` is what keeps the bar seated if the head's
        // layering moves again: it moved today, when the dark outer backing came off and the ring
        // became the outermost thing a head draws.
        const float diamond_half =
            (metrics.headSize() / 2.0f) - noteBorderThickness(metrics.headSize());
        g.setColour(style[Ink::BorderInner]);
        g.fillRect(
            juce::Rectangle<float>{
                onset_x - diamond_half, center_y - diamond_half, 3.0f, diamond_half * 2.0f
            });
    }

    // The X reports this note's OWN mute state and nothing else — a mark that means one thing on
    // one note and another elsewhere is a mark the reader has to disambiguate; the plectrum
    // silhouette already says what a scrape is. The chart rules reject a mute on a pick-slide
    // note outright, so a scrape passes two clear flags here and draws no X at all.

    drawMuteIcon(g, metrics, style, note.palm_mute, note.dead, onset_x, center_y);
}

// Draws the complete normal-note head stack in its established furniture-before-attack order.
void drawNoteHead(
    juce::Graphics& g, const TabLaneMetrics& metrics, const StringStyle& style,
    const common::core::NoteViewState& note, const float onset_x, const float center_y)
{
    drawNoteHeadBase(g, metrics, style, note, onset_x, center_y);
    drawNoteHeadFretPlate(g, metrics, style, note, onset_x, center_y, 1.0f);
    drawNoteHeadFretNumber(g, metrics, style, note, onset_x, center_y);
    // The beside-head satellite draws LAST here, over every other mark including the dead X —
    // the opposite of the order the 3D head uses, and deliberately so. The surfaces are not
    // disagreeing about one rule; they are answering two different questions. On the highway
    // every mark stacks CONCENTRIC on the head, so the order decides which mark survives a
    // collision outright, and there the X wins because a broken X reads as a different mark.
    // In this lane the satellite has its own slot up and left of the head and meets the X only
    // at one arm's tip: covering the small mark entirely costs the reader more than clipping the
    // end of a long stroke whose identity is already legible. The shared law is "whichever mark
    // cannot afford to be cut draws last", and that resolves to a different mark on each surface
    // because the geometry differs.
    drawAttackIcon(g, metrics, style, note, onset_x, center_y);
}

// Draws one hand-shape span as narrow rails along the lane's top and bottom edges for the
// span's duration — blue for chord shapes, purple for arpeggios — echoing the 3D highway's
// shape rails at the hand-window fret lines (a departure from Charter's full-height tint, which
// read as an ugly wall of color). The template name, when present, rides the host's name-chip
// band (the editor's timeline ruler), not the lane itself.
void drawShapeSpan(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ShapeViewState& shape)
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
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::FhpViewState& fhp)
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

// Draws the capo chip pinned in the lane's top-left corner, in the FHP chips' boxed style. The
// chart stores absolute frets with 0 meaning the capo'd open string, so nothing else in the
// drawn content says where the string floor sits — this chip is the 2D capo indication (roadmap
// 25-Q6, crude first treatment). Pinned to the bounds rather than the timeline because the capo
// has no time; drawn last so scrolling content passes under it.
void drawCapoChip(juce::Graphics& g, const TabLaneMetrics& metrics, const int capo)
{
    if (capo <= 0 || !metrics.draw_text)
    {
        return;
    }

    const juce::String text = "Capo " + juce::String{capo};
    const float width = static_cast<float>(textWidth(metrics.label_font, text)) + 6.0f;
    constexpr float height = 12.0f;
    const juce::Rectangle<float> box{
        static_cast<float>(metrics.bounds.getX()) + 2.0f,
        static_cast<float>(metrics.bounds.getY()) + 1.0f,
        width,
        height
    };
    g.setColour(juce::Colour{0xff2a2f36});
    g.fillRoundedRectangle(box, 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(metrics.label_font);
    g.drawText(text, box, juce::Justification::centred);
}

// The visible time span one paint call can show: the clip, held to the lane's own bounds and
// widened by the pixel slack heads and icons reach around their onset, since an event whose onset
// sits just outside the clip still has ink inside it. The clip is intersected here so a host
// drawing the lane inside a larger component cannot have that component's other columns read as
// visible time.
[[nodiscard]] common::core::TimeRange tabVisibleSpan(
    const TabLaneMetrics& metrics, juce::Rectangle<int> clip_bounds)
{
    // Divided by immediately below, exactly as makeTabLaneMetrics divides by it.
    assert(metrics.bounds.getWidth() > 0);

    const juce::Rectangle<int> clip = clip_bounds.getIntersection(metrics.bounds);
    const double duration = metrics.visible_timeline.duration().seconds;
    const double seconds_per_pixel = duration / static_cast<double>(metrics.bounds.getWidth());
    const double slack_seconds =
        static_cast<double>(metrics.max_note_height) * 3.0 * seconds_per_pixel;
    // Clip columns relative to the lane's left edge, which is where x() measures time from.
    const int clip_from = clip.getX() - metrics.bounds.getX();
    const int clip_to = clip.getRight() - metrics.bounds.getX();
    return common::core::TimeRange{
        .start =
            common::core::TimePosition{
                metrics.visible_timeline.start.seconds +
                static_cast<double>(clip_from) * seconds_per_pixel - slack_seconds
            },
        .end = common::core::TimePosition{
            metrics.visible_timeline.start.seconds +
            static_cast<double>(clip_to) * seconds_per_pixel + slack_seconds
        },
    };
}

} // namespace

// Converts the shared palette authority to JUCE colors at the paint core's boundary; the
// lane-window logic lives with the palette (string_color_palette.h).
juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return juce::Colour{stringLaneColor(
        displayed_string, displayed_string_count, charterClassicPalette())};
}

// Rationale lives on the declaration in tab_paint_core.h. The silhouette comes from headShapeFor,
// the same authority the drawn head uses, which is the whole point of exporting this.
void strokeTabNoteHeadOutline(
    juce::Graphics& g, const common::core::NoteViewState& note, const float center_x,
    const float center_y, const float extent, const float stroke_thickness)
{
    const float half = extent / 2.0f;
    juce::Path outline;
    switch (headShapeFor(note))
    {
        case HeadShape::Diamond:
            outline.startNewSubPath(center_x, center_y - half);
            outline.lineTo(center_x + half, center_y);
            outline.lineTo(center_x, center_y + half);
            outline.lineTo(center_x - half, center_y);
            outline.closeSubPath();
            break;
        case HeadShape::Plectrum:
            outline = plectrumPath(center_x, center_y, extent);
            break;
        case HeadShape::Round:
            outline.addEllipse(center_x - half, center_y - half, extent, extent);
            break;
    }
    g.strokePath(outline, juce::PathStrokeType{stroke_thickness});
}

// Rationale lives on the declaration in tab_paint_core.h. The two grounds stay internal on
// purpose: they are KNOWN backgrounds the host cannot mispair with its inks. Dark is
// 0xff101010, the lane's own established near-black; light is pure white, so the invalid red reads
// at the error idiom's full pop and the PLATE POLARITY FLIP itself signals invalid even in full
// monochrome. It uses the same glance mechanism the mute plate-flip design established.
void paintTabPendingEntryBox(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState* note,
    const float center_x, const float center_y, const juce::String& text, const bool light_plate,
    const juce::Colour text_color, const juce::Colour border_color)
{
    // The box rides the head's own digit placement — the plectrum raise included — so the
    // provisional number sits exactly where the committed one will land. An empty insert slot
    // has no head and takes the string-line center, which is where its plain round head's digit
    // will sit.
    const float digit_raise =
        note != nullptr ? headDigitRaise(headShapeFor(*note), metrics.headSize()) : 0.0f;
    const juce::Rectangle<float> plate =
        headTextPlate(metrics, text, center_x, center_y - digit_raise);
    // The valid ground is the lane's own near-black (the head backing's ink) and the invalid one is
    // the light plate, the same two inks every plated digit already wears.
    g.setColour(light_plate ? juce::Colours::white : g_note_background_color);
    g.fillRect(plate);
    g.setColour(border_color);
    g.drawRect(plate, 1.0f);
    if (metrics.draw_text)
    {
        g.setColour(text_color);
        g.setFont(metrics.fret_font);
        g.drawText(text, plate, juce::Justification::centred);
    }
}

// Rationale lives on the declaration in tab_paint_core.h.
juce::String tabNoteHeadText(const common::core::NoteViewState& note, const int fret_at_head)
{
    const common::core::SoundingPosition sounding =
        common::core::soundingPositionAt(note.harmonic_node, note.attack, note.fret, fret_at_head);
    if (!sounding.at_node)
    {
        return juce::String{fret_at_head};
    }
    // Through the one node-label authority (shared with the 3D floor numbers), so a node reads
    // identically on both surfaces; the shared sounding rule already carried it to this head's
    // own stop, so an onset and a junction of one gesture state the same quantity.
    return juce::String{common::core::harmonicNodeText(sounding.position)};
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
    return tabStringColor(common::core::displayedLane(chart_string, extra_lanes), displayed_count);
}

TabLaneMetrics makeTabLaneMetrics(
    juce::Rectangle<int> bounds, common::core::TimeRange visible_timeline, int displayed_count,
    int chart_string_count, TabLaneStyle style)
{
    // The header states these as preconditions and the geometry below DIVIDES by both, so a
    // violation is undefined behaviour rather than a wrong picture: an empty bounds zeroes the
    // seconds-per-pixel denominator and a non-positive count zeroes the lane-height one. Asserted
    // rather than clamped, because a host asking for a zero-lane tab has a bug upstream that a
    // silently invented lane would hide.
    assert(!bounds.isEmpty());
    assert(displayed_count > 0);

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
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const std::vector<double>& prefix_max_end_seconds, const TabDrawnNote& drawn_note)
{
    // Stated as a precondition in the header; the lane lines below index by string.
    assert(tab.string_count > 0);

    // The clip held to the lane's own bounds: the lane occupies only those bounds, so a host
    // drawing it inside a larger component must not have that component's other columns carry
    // lane lines. The visible SPAN widens it further by the glyph slack — one rule, shared with
    // every host that culls chrome alongside this notation.
    const juce::Rectangle<int> clip = g.getClipBounds().getIntersection(metrics.bounds);
    const common::core::TimeRange span = tabVisibleSpan(metrics, clip);
    const double span_start = span.start.seconds;
    const double span_end = span.end.seconds;

    // Bracket geometry shared by the string-line gaps below and the bracket pass further
    // down; the values depend only on the lane metrics, not on the individual note.
    const float bracket_size = metrics.headSize();
    const float bracket_border = std::max(1.0f, bracket_size / 15.0f);
    const float bracket_radius = bracket_size / 2.0f + bracket_border;
    constexpr int bracket_bar = g_arpeggio_bracket_thickness;

    // Both shape passes stop at the same index, since a span starting past the window cannot show.
    // The arpeggio pass also skips every span starting before the window, because a bracket sits AT
    // the span start; the rails pass cannot, because a span that began earlier still covers the
    // window and nothing orders spans by end — its exact first index would need a prefix maximum of
    // span ends, the way the notes have one.
    const auto shape_start = &common::core::ShapeViewState::start_seconds;
    const auto shapes_end =
        std::ranges::upper_bound(tab.shapes, span_end, std::ranges::less{}, shape_start);

    // Every visible bracket, resolved once: the lane lines hide inside each one so the "[ fret ]"
    // marks read on a clean background, and the bracket pass draws the identical rectangles.
    std::vector<ArpeggioBracket> brackets;
    for (const common::core::ShapeViewState& shape : std::ranges::subrange{
             std::ranges::lower_bound(tab.shapes, span_start, std::ranges::less{}, shape_start),
             shapes_end
         })
    {
        if (!shape.arpeggio)
        {
            continue;
        }

        const float start_x = metrics.x(shape.start_seconds);
        // The WIDEST digit in this span, so every side chip in the column is the same width and the
        // stack closes on one straight right wall. Sizing each to its own digit leaves a two-digit
        // string jutting past its neighbours.
        int span_digit_width = 0;
        if (metrics.draw_text)
        {
            for (const common::core::ShapeStringViewState& note : shape.strings)
            {
                span_digit_width = std::max(
                    span_digit_width, textWidth(metrics.fret_font, juce::String{note.fret}));
            }
        }
        for (const common::core::ShapeStringViewState& arpeggio_note : shape.strings)
        {
            const int displayed =
                common::core::displayedLane(arpeggio_note.string, metrics.extra_lanes);
            if (displayed < 1 || displayed > metrics.displayed_count)
            {
                continue;
            }

            const int digit_width =
                metrics.draw_text ? textWidth(metrics.fret_font, juce::String{arpeggio_note.fret})
                                  : 0;
            const common::core::NoteViewState* const head =
                headAtSpanStart(tab, shape.start_seconds, arpeggio_note.string);
            bool side_slot = false;

            const int bar_left =
                juce::roundToInt(start_x - bracket_radius - static_cast<float>(bracket_bar) / 2.0f);
            const int bar_right =
                juce::roundToInt(start_x + bracket_radius + static_cast<float>(bracket_bar) / 2.0f);
            int digit_left = juce::roundToInt(start_x) - (digit_width / 2);
            int mark_right = bar_right;
            int drawn_width = digit_width;

            // WHERE the posture states, decided per string by what sounds on it at the span start.
            // The defaults above are the silent-string case: the posture keeps the centre a fret
            // number belongs in, and needs no ground there because the technique marks that would
            // cross it clip against the bracket's own columns.
            if (head != nullptr)
            {
                if (common::core::rightHandOnset(head->attack) && head->fret != arpeggio_note.fret)
                {
                    // The tap is what rings, so it keeps the centre; the fretting hand has NOT
                    // moved, so its fret is still true and takes the side slot beside the bracket.
                    side_slot = true;
                    digit_left = bar_right + g_arpeggio_posture_gap;
                    drawn_width = span_digit_width;
                    mark_right = digit_left + span_digit_width + g_arpeggio_posture_gap;
                }
                else
                {
                    // Either the head already states this fret, or a fretting-hand onset moved
                    // the hand off the template — in which case the posture is no longer held and
                    // stating it would be false.
                    drawn_width = 0;
                }
            }

            brackets.push_back(
                ArpeggioBracket{
                    .note = arpeggio_note,
                    .center_x = start_x,
                    .digit_left = digit_left,
                    .digit_width = drawn_width,
                    .side_slot = side_slot,
                    .bar_left = bar_left,
                    .bar_right = bar_right,
                    .mark_right = mark_right
                });
        }
    }

    drawStringLines(g, metrics, clip, brackets);

    for (const common::core::ShapeViewState& shape :
         std::ranges::subrange{tab.shapes.begin(), shapes_end})
    {
        if (shape.end_seconds >= span_start)
        {
            drawShapeSpan(g, metrics, shape);
        }
    }

    const LaneStyles lane_styles = makeLaneStyles(metrics);

    const auto [first, last] =
        common::core::visibleEventRange(tab.notes, prefix_max_end_seconds, span_start, span_end);

    // The note each pass below draws, resolved once for the whole call so no pass restates the
    // fallback: the host's choice where it composes two forms of one chart, the state's own note
    // otherwise. The index range above stays the state's own either way — presentation moves no
    // onset and adds or removes no note, so the forms align by index and share these search keys.
    const auto note_at = [&](std::size_t index) -> const common::core::NoteViewState& {
        return drawn_note ? drawn_note(index) : tab.notes[index];
    };

    // Floating labels collected during the note passes and drawn above every head.
    std::vector<LabelChip> slide_labels;
    std::vector<LabelChip> bend_chips;

    // Tails first so normal heads cover their own tail starts (Charter's noteTails layer). A ghost
    // instead draws its head here inside the same flattened group as its tail.
    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = note_at(index);
        if (note.end_seconds < span_start)
        {
            continue;
        }

        const StringStyle& style = lane_styles(note.string);
        const float center_y = metrics.laneY(note.string);
        const float onset_x = metrics.x(note.start_seconds);

        // A ghost's opaque tail, marks and head are flattened together, then the finished note is
        // composited once. Per-ink alpha would let the already-drawn tail show through the head.
        const bool grouped = common::core::isGhosted(note.emphasis);
        const float note_opacity = grouped ? g_ghost_opacity : 1.0f;
        const float fret_plate_opacity = grouped ? g_ghost_fret_plate_opacity : 1.0f;
        const juce::Rectangle<int> group_bounds{
            juce::roundToInt(onset_x - metrics.headSize()),
            juce::roundToInt(center_y - metrics.lane_height),
            juce::roundToInt(metrics.x(note.end_seconds) - onset_x + (2.0f * metrics.headSize())),
            juce::roundToInt(2.0f * metrics.lane_height)
        };
        std::optional<ScopedTransparencyLayer> group;
        if (grouped && g.clipRegionIntersects(group_bounds))
        {
            group.emplace(g, group_bounds, note_opacity);
        }

        drawNoteTail(g, metrics, style, note, onset_x, center_y);

        // The TECHNIQUE marks riding the tail — slide diagonals, bend curves, the vibrato sine —
        // clip against every arpeggio bracket on this string: a posture mark states where the hand
        // is, and a diagonal cutting through it muddies the one column the reader is decoding. The
        // tail's own body (ribbon, tremolo teeth) shows through untouched, so the sustain still
        // plainly continues; only the marks yield, the same way the lane line already does. The
        // deferred label chips are collected here but drawn later, outside this clip, so a
        // clipped-away leg keeps its floating fret label.
        //
        // Scoped so the clip is gone before this note's group layer closes: the head below has to
        // be inside the group but outside the technique band, and a saved state still alive when
        // a transparency layer ends corrupts the composite silently on every renderer.
        {
            juce::Graphics::ScopedSaveState technique_clip{g};
            // And they never leave the tail's INTERIOR — the band between the edge rails. Every
            // mark already COMPRESSES its geometry to fit it (the sine's and bend polyline's
            // swing, the diagonals' anchors), but stroke corners and antialiasing still overshoot
            // by a pixel; a mark riding onto a rail reads as leaking out of the sustain, so the
            // clip is the guarantee the geometry aims for.
            const TailInterior technique_band = tailInterior(metrics, center_y);
            const int band_top = static_cast<int>(std::floor(technique_band.top));
            g.reduceClipRegion(
                juce::Rectangle<int>{
                    metrics.bounds.getX(),
                    band_top,
                    metrics.bounds.getWidth(),
                    static_cast<int>(std::ceil(technique_band.bottom)) - band_top
                });
            for (const ArpeggioBracket& bracket : brackets)
            {
                if (bracket.note.string == note.string)
                {
                    g.excludeClipRegion(
                        juce::Rectangle<int>{
                            bracket.bar_left,
                            juce::roundToInt(center_y - metrics.lane_height / 2.0f),
                            bracket.bar_right - bracket.bar_left,
                            juce::roundToInt(metrics.lane_height)
                        });
                }
            }
            drawVibratoSine(g, metrics, style, note, onset_x, center_y);
            // An unpitched slide label states a fret, so its box uses the plate weight while its
            // text stays fully opaque.
            drawSlideLines(
                g, metrics, style, note, onset_x, center_y, slide_labels, fret_plate_opacity);
            drawBendLines(g, metrics, style, note, onset_x, center_y, bend_chips, note_opacity);
        }

        if (grouped)
        {
            drawSlideWaypointHeadShapes(g, metrics, style, note, center_y);
            drawNoteHeadBase(g, metrics, style, note, onset_x, center_y);
            // Attack badges remain in the ghost group. Their placement explicitly clears the fret
            // window, so the later fret overlays cannot obscure them.
            drawAttackIcon(g, metrics, style, note, onset_x, center_y);

            // Close the note group before drawing fret plates at their middle weight and fret
            // numbers fully opaque.
            group.reset();
            drawSlideWaypointFretNumbers(g, metrics, style, note, center_y);
            drawNoteHeadFretPlate(g, metrics, style, note, onset_x, center_y, fret_plate_opacity);
            drawNoteHeadFretNumber(g, metrics, style, note, onset_x, center_y);
        }
    }

    // Arpeggio spans draw "( fret )" bracket marks around every posture string at the
    // bracket start. Onsets carry no vertical bars — the heads themselves already mark them, so
    // the span rails and these brackets are the only shape furniture.
    //
    // Left and right square brackets hugging the head's ring, in the head's muted interior color
    // so they mark the posture without competing with real heads. A string sounded exactly at the
    // start keeps its full head (drawn by the note pass) inside the brackets.
    //
    // The held fret is stated ALWAYS, in its own slot outboard of the closing bar, and that slot
    // is the whole reason the statement can be unconditional. The digit used to sit dead centre —
    // the head's own box — so it had to yield whenever a note landed there. That yield looks
    // harmless, and mostly is: a head draws its own number, so the column of digits stays complete
    // either way. What it loses is the case where the two numbers DIFFER, and that case is a tap
    // over a held shape — ordinary rather than rare, since the arrival rule names "a held chord
    // under two-hand tapping" as one of the things that MAKE a span an arpeggio. Yielding there
    // prints the tapped fret and silently drops where the fretting hand is posted, which is the
    // one thing a posture bracket exists to say. Giving the two claims two slots makes the
    // conflict unrepresentable instead of arbitrated: the head's centre carries the sounding fret,
    // the satellite carries the posture, and where they agree, saying it twice in two registers is
    // what makes the shape plain. Outboard RIGHT because every other side is spoken for — the
    // attack icons own the upper-left shoulder, the floating chips own the space above, and the
    // left is where the previous note's head and its arriving sustain ribbon live.
    //
    // The bracket bars are unchanged by all this; only the lane-line gap grew to cover the digit.
    //
    // The note's VISIBLE top and bottom are the bright ring's edges: the head's outermost layer is
    // the near-black backing, which melts into the dark lane. The brackets stop a bar-width inside
    // that visible edge, so they never rise above or dip below what reads as the note. Every
    // rectangle snaps to whole pixels so the brackets stay perfectly square instead of
    // antialiasing into fuzz.
    const float bracket_half_height =
        bracket_size / 2.0f - bracket_border - static_cast<float>(bracket_bar);
    const int bracket_serif = juce::roundToInt(bracket_size / 8.0f) + bracket_bar;
    for (const ArpeggioBracket& bracket : brackets)
    {
        // Posture brackets are SHAPE furniture, not a note's ink: they state where the hand is
        // posted, which is as true under a ghosted strum as under any other. They take the plain
        // style whatever the notes inside them are struck at.
        const StringStyle& style = lane_styles(bracket.note.string);
        const float center_y = metrics.laneY(bracket.note.string);
        const int top = juce::roundToInt(center_y - bracket_half_height);
        const int bottom = juce::roundToInt(center_y + bracket_half_height);
        // The tail's own edge colour. The brackets used to take the note FILL, chosen to sit
        // quietly against a bright tail; once the tail's fill drops to the waypoint heads' dark
        // that reasoning inverts and the marks go dark-on-dark. The edge is the one value in the
        // string's palette already chosen to read against a tail, and it is left at full
        // brightness by the fill change, so it stays legible by construction.
        const juce::Colour bracket_ink = style[Ink::TailEdge];

        g.setColour(bracket_ink);
        g.fillRect(bracket.bar_left, top, bracket_bar, bottom - top);
        g.fillRect(bracket.bar_left, top, bracket_serif, bracket_bar);
        g.fillRect(bracket.bar_left, bottom - bracket_bar, bracket_serif, bracket_bar);
        g.fillRect(bracket.bar_right - bracket_bar, top, bracket_bar, bottom - top);
        g.fillRect(bracket.bar_right - bracket_serif, top, bracket_serif, bracket_bar);
        g.fillRect(
            bracket.bar_right - bracket_serif, bottom - bracket_bar, bracket_serif, bracket_bar);

        if (metrics.draw_text && bracket.digit_width > 0)
        {
            const juce::String text{bracket.note.fret};
            const juce::Rectangle<float> box{
                static_cast<float>(bracket.digit_left),
                center_y - bracket_size / 2.0f,
                static_cast<float>(bracket.digit_width),
                bracket_size
            };
            // The side chip's ground: the tail's own interior in the tail's own fill, so it reads
            // as a clean stretch OF the ribbon rather than an object on it, masking whatever
            // technique mark crosses its columns. Only the SIDE slot needs one — it sits outside
            // the bracket bars, past the clip that already keeps technique marks out of the
            // bracket's own columns, which is all the ground a centred digit requires.
            //
            // The ground is also what lets the digit below be plain white. A posture digit has to
            // read on every string, and the per-string inks do not carry that on their own — the
            // bracket's own fill measures barely 18 peak dL* against the lane band on the red
            // string. Giving the digit a known ground answers that once, for all six strings,
            // instead of hunting a neutral ink that clears every one of them.
            if (bracket.side_slot)
            {
                const TailInterior interior = tailInterior(metrics, center_y);
                const int patch_top = juce::roundToInt(interior.top);
                const int patch_bottom = juce::roundToInt(interior.bottom);
                g.setColour(style[Ink::Tail]);
                g.fillRect(
                    bracket.bar_right,
                    patch_top,
                    bracket.mark_right - bracket.bar_right,
                    patch_bottom - patch_top);
            }
            g.setColour(juce::Colours::white);
            g.setFont(metrics.fret_font);
            g.drawText(text, box, juce::Justification::centred);
        }
    }

    for (std::size_t index = first; index < last; ++index)
    {
        const common::core::NoteViewState& note = note_at(index);
        if (note.end_seconds < span_start)
        {
            continue;
        }

        // A grouped ghost already drew its head opaquely over its tail before the group faded.
        if (common::core::isGhosted(note.emphasis))
        {
            continue;
        }

        const StringStyle& style = lane_styles(note.string);
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
                std::optional<ScopedTransparencyLayer> chip_layer;
                const juce::Rectangle<int> chip_bounds = box.getSmallestIntegerContainer();
                if (chip.opacity < 1.0f && g.clipRegionIntersects(chip_bounds))
                {
                    chip_layer.emplace(g, chip_bounds, chip.opacity);
                }
                g.setColour(chip.background);
                g.fillRect(box);
                if (chip.border != chip.background)
                {
                    g.setColour(chip.border);
                    g.drawRect(box, 1.0f);
                }
                if (chip.opaque_ink)
                {
                    chip_layer.reset();
                }
                g.setColour(chip.ink);
                g.drawText(chip.text, box, juce::Justification::centred);
            }
        };
    if (metrics.draw_text)
    {
        draw_chips(slide_labels, metrics.fret_font, 3.0f);
        draw_chips(bend_chips, metrics.bend_font, 2.0f);
    }

    // The capo chip shares the lane's top-left band with the fret-hand markers, and a placement
    // at the very start of the visible window lands under it. The chip goes down FIRST so the
    // marker wins that overlap: the capo is static information the reader learns once, while the
    // marker's fret is time-critical and scrolls away. (Both wanting the same corner is noted in
    // roadmap 25-Q6 for the real capo treatment.)
    drawCapoChip(g, metrics, tab.capo);

    // Each marker shows at its own position and they ascend in time, so the visible ones are one
    // bounded slice rather than a walk of the whole song's placements.
    const auto fhp_seconds = &common::core::FhpViewState::seconds;
    for (const common::core::FhpViewState& fhp : std::ranges::subrange{
             std::ranges::lower_bound(
                 tab.fret_hand_positions, span_start, std::ranges::less{}, fhp_seconds),
             std::ranges::upper_bound(
                 tab.fret_hand_positions, span_end, std::ranges::less{}, fhp_seconds)
         })
    {
        drawFhpMarker(g, metrics, fhp);
    }
}

} // namespace rock_hero::common::ui
