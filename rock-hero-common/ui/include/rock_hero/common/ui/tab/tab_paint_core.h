/*!
\file tab_paint_core.h
\brief Shared JUCE notation paint core rendering the 2D tablature for both products.

The one designated juce_graphics-bearing public header in rock-hero-common/ui (the 30-Q1
amendment in docs/design/architectural-principles.md "UI Modules"): the editor tab lane and the
game tab strips must produce identical notation pixels, so the rasterizer itself is shared and
each host supplies only bounds, timeline mapping, and state.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <string>
#include <vector>

namespace rock_hero::common::ui
{

/*!
\brief How much wider the WIDE vibrato tier swings than the ordinary one on the tab lane.

The lane's twin of \ref rock_hero::common::core::g_highway_wide_vibrato_depth_multiplier, and one
of the two numbers a sighting of the tier moves. Stated once and read in BOTH directions here: the
wide wave fills the tail's technique band and the ordinary one is that divided by this, because the
band is a hard clip on this surface — a wave drawn past it would truncate its crests and read as a
square wave rather than as a wider shake, which is why the lane cannot simply scale the wide tier
up the way the board does.
*/
inline constexpr float g_wide_vibrato_swing_multiplier = 2.0f;

/*!
\brief Returns the base display color for one string lane as a JUCE color.

The six highest lanes take the Charter Classic preset's standard colors anchored at the
sixth-highest lane; lower lanes continue with the extended tier (see stringLaneColor). This is
the paint core's JUCE conversion of the shared palette authority.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\return Base lane color the tablature style derives its surfaces from.
*/
[[nodiscard]] juce::Colour tabStringColor(int displayed_string, int displayed_string_count);

/*!
\brief Returns the hand-shape mark color shared by the tab lane and name chips above it.

The Charter hand-shape base brightened so the narrow span rails and the chord/arpeggio name
chips read clearly against dark chrome; hosts drawing name chips derived from the same tab
projection must agree on it so a chip visually belongs to the rails below it.

\param arpeggio True for arpeggio spans (purple); false for chord spans (blue).
\return Opaque mark color.
*/
[[nodiscard]] juce::Colour tabShapeMarkColor(bool arpeggio);

/*!
\brief Returns the number a head carries for this note stopped at `fret_at_head`.

Any harmonic whose node lies on the neck names its **node** rather than its fret, because the node
is what sets the pitch. That is \ref rock_hero::common::core::nodeIsOnNeck, so it covers a tap
harmonic and an artificial or harp harmonic over a real stop as well as a natural one — the stop is
a genuine fret in those cases, and the label still names the node, because the node is what sounds.
One decimal is exactly enough: it separates every distinct node through the 17th harmonic, far
past the ~8th a fingertip can still isolate. A trailing ".0" is dropped so the common 12 / 7 / 5
positions stay as narrow as an ordinary fret number.

A **pinch** keeps its fret: its node sits off the neck over the pickups, and 2D has no axis to place
that on, so drawing it would name a fret the hand is nowhere near (roadmap 25-Q5).

The stop is a parameter because one gesture has more than one head: the onset passes the note's own
fret, and a linked slide junction passes the fret the glide has reached, so every head of a gesture
states the same QUANTITY (a harmonic labels nodes at all of them, not a node at the onset and a raw
fret at the junctions).

\param note Projected note to label.
\param fret_at_head Fret the head being labeled sits at — `note.fret` at the onset, the keyframe's
       fret at a linked junction.
\return Head text, never empty.
*/
[[nodiscard]] juce::String tabNoteHeadText(
    const common::core::NoteViewState& note, int fret_at_head);

/*!
\brief Returns the vertical center of one string lane inside JUCE component bounds.
\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param bounds Full tablature lane bounds.
\return Vertical lane center in the bounds' coordinate space.
*/
[[nodiscard]] inline float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    return tabLaneCenterY(
        displayed_string,
        displayed_string_count,
        static_cast<float>(bounds.getY()),
        static_cast<float>(bounds.getHeight()));
}

/*!
\brief One of the lane's fonts, and the only way text is measured or drawn on this surface.

THE ONE STATEMENT of how a label sits on its line. JUCE centres text by the font's
ascent-plus-descent LINE box (juce_GlyphArrangement.cpp, justifyGlyphs, whose bounding box is each
glyph's line box rather than its outline), but everything this lane prints — fret numbers, node
labels, bend amounts, string names — lives between the baseline and the cap line and carries no
descender ink at all. A line-box-centred number therefore hangs below the string it labels by
(ascent - descent - ink height) / 2, and the software renderer rounds that to a WHOLE ROW
(juce_RenderingHelpers.h, drawGlyph passes `roundToInt` of the draw position's y to fillEdgeTable),
so a third of a pixel of asymmetry is drawn as a full pixel of it.

The correction is measured, once, from a reference figure — never estimated as a fraction of the
font height, which is what a hand-tuned nudge would be — and it is carried BY the font so no drawer
can take one without the other. That is why the font itself is not exposed: `draw` is the only way
text reaches this lane, so a second placement rule cannot be written beside this one.

One reference figure rather than each label's own ink, deliberately: a lane where "1" and "8" sat
at different heights would be worse than one where both sit at the zero's, and the spread across
every glyph this lane prints is 0.08 px. It also keeps the measurement out of the per-frame path —
it happens once per metrics build, not once per number drawn.
*/
class TabLaneFont
{
public:
    /*! \brief Constructs the placeholder font a default-constructed \ref TabLaneMetrics carries. */
    TabLaneFont();

    /*!
    \brief Constructs a lane font, measuring its reference figure's ink.
    \param font Font to draw and measure this lane's text with.
    */
    explicit TabLaneFont(juce::Font font);

    /*!
    \brief Height the font was built at — JUCE's ascent-plus-descent line box, not a cap height.

    What callers size a plate or a chip against, exactly as they did when this was a bare
    juce::Font; \ref inkHeight is the separate question of how much of that box a glyph fills.

    \return Font height in pixels.
    */
    [[nodiscard]] float height() const noexcept;

    /*!
    \brief Measured ink height of the reference figure, in pixels.

    How tall a printed number or capital really is — about six tenths of \ref height, and the
    number to use for any clearance a mark has to keep from a label's ink.

    \return Reference figure's ink height in pixels.
    */
    [[nodiscard]] float inkHeight() const noexcept;

    /*!
    \brief Width one line of text occupies, rounded up so reserved space never truncates a glyph.
    \param text Text to measure.
    \return Advance width in whole pixels.
    */
    [[nodiscard]] int width(const juce::String& text) const;

    /*!
    \brief Draws one line of text centred in `box`, by its INK rather than by its line box.

    The box is the one the caller would centre the text in geometrically; whatever that box is
    centred on — a string line, a plate, a chip — is what the glyphs' ink ends up centred on.
    Drawing is in the graphics context's current colour.

    \param g Graphics context to draw into.
    \param text Text to draw; an empty string draws nothing.
    \param box Box the text is centred in, in the context's coordinate space.
    */
    void draw(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box) const;

private:
    // Initialized via FontOptions because JUCE 8 deprecates the default Font constructor.
    juce::Font m_font{juce::FontOptions{}};

    // Ink box of the reference figure, relative to the baseline: negative above it. Measured in
    // the constructor so the per-frame path never pays for a glyph outline.
    juce::Rectangle<float> m_reference_ink{};
};

/*!
\brief Lane geometry plus the JUCE-only facts one paint call needs.

Extends the framework-free TabLaneGeometry (which layout-manifest consumers share) with the
component bounds and the derived fonts, so every drawer of one paint call reads one authority.
*/
struct TabLaneMetrics : TabLaneGeometry
{
    /*! \brief Full tablature lane bounds in the graphics context's space. */
    juce::Rectangle<int> bounds;

    // The placeholder fonts are replaced by makeTabLaneMetrics before any drawing.

    /*! \brief Bold fret-number font derived from the note height. */
    TabLaneFont fret_font;

    /*! \brief Bend amount chip font derived from the note height. */
    TabLaneFont bend_font;

    /*! \brief Bold label font for hand-shape and fret-hand-position chips. */
    TabLaneFont label_font;

    /*!
    \brief Base color for a chart string, accounting for extra user lanes below the chart.
    \param chart_string One-based chart string, 1 = the chart's lowest string.
    \return Base lane color for the string.
    */
    [[nodiscard]] juce::Colour baseColor(int chart_string) const;
};

/*!
\brief Derives the metrics for one tablature paint call.

\param bounds Full tablature lane bounds; must not be empty.
\param visible_timeline Timeline range represented by the width; must have positive duration.
\param displayed_count Number of displayed lanes; must be positive.
\param chart_string_count String count declared by the displayed chart.
\param style Optional style variants; the default reproduces the editor lane exactly.
\return Metrics every drawer of the paint call reads.
*/
[[nodiscard]] TabLaneMetrics makeTabLaneMetrics(
    juce::Rectangle<int> bounds, common::core::TimeRange visible_timeline, int displayed_count,
    int chart_string_count, TabLaneStyle style = {});

/*!
\brief Strokes the outline of the silhouette this note's head is drawn with.

For host chrome that must trace a head it did not draw — the editor's selection ring is the one
caller today. The silhouette is chosen by the same rule the head itself uses, so a ring cannot
disagree with the head under it: re-deriving "diamond if it has a node, else a circle" in the host
left every pick slide wearing a circular ring around a plectrum once the scrape head shipped.

\param g Graphics context to draw into.
\param note Note whose head silhouette is wanted.
\param center_x Head center on the time axis.
\param center_y Head center on the lane's string line.
\param extent Head size, as \ref TabNoteLayout::head_size reports it.
\param stroke_thickness Outline thickness in pixels.
*/
void strokeTabNoteHeadOutline(
    juce::Graphics& g, const common::core::NoteViewState& note, float center_x, float center_y,
    float extent, float stroke_thickness);

/*!
\brief Strokes the outline of the silhouette one posture bracket is drawn with.

The bracket twin of \ref strokeTabNoteHeadOutline, and it exists for the same one caller: a
silently-held stop has no head, so its selection edge is the BRACKET wearing it (user ruling
2026-08-27). The two square-bracket glyphs are traced on their own silhouette — down each bar and
around its serifs — so the accent lands on the mark and the empty centre where no head exists stays
empty, which a rectangle around the pair could not do.

Where the hold's own digit was displaced outboard, the trace runs on to enclose that column too, so
the whole mark reads as selected and the inked extent matches the clickable one. That extent is
read from the layout rather than re-decided here, which is what keeps the edge and the hit box one
answer; the bars themselves come from \ref TabLaneGeometry::bracketColumnsAt, the same columns the
fill draws.

\param g Graphics context to draw into.
\param geometry Lane geometry the bracket was painted with.
\param layout The hold's bracket layout, as \ref tabSilentHoldLayout reports it.
\param stroke_thickness Outline thickness in pixels.
*/
void strokeTabBracketOutline(
    juce::Graphics& g, const TabLaneGeometry& geometry, const TabSilentHoldLayout& layout,
    float stroke_thickness);

/*!
\brief Draws the editor's pending fret entry box: the mute number-plate's own geometry and font
carrying a provisional value.

Host chrome, not notation — the game renders no keyboard entry — but exported from the core
rather than restated in the host so the provisional digit's typography and placement CANNOT
drift from the committed head's (the one-primitive rule of the pending-entry design; the insert
ghost's shape drifted exactly this way once). The plate rect is the same authority the mute
number-plate draws and the font is the head digit's own. The GROUND flips with validity, and
both grounds are known and internal: a valid value sits on the lane's near-black exactly like a
committed plated digit, and an invalid one flips to a white plate — red-on-white is the error
idiom at full contrast, and the plate polarity flip itself carries the signal in full
monochrome, the glance mechanism the mute plate-flip design established. The border and inks
are the host's, because pending is an editor state and this core owns no editor colors.

The box follows the head's own digit PLACEMENT too: a plectrum raises its number to fit the
silhouette, so the box over a scrape rides the same raise — the provisional digit must sit
exactly where the committed one will land.

\param g Graphics context to draw into.
\param metrics Metrics of the lane being painted.
\param note Note whose head the box rides, or null at an empty insert slot; supplies the head
       shape the digit placement follows.
\param center_x Box center on the time axis — a head's onset x, or an empty insert slot's x.
\param center_y The head's center on the lane's string line; the box derives the digit's own
       center from it.
\param text Provisional value exactly as typed.
\param light_plate True flips the box to the white invalid ground; false is the dark valid one.
\param text_color Text ink: the host's digit white while the value would apply, red when not.
\param border_color Box border: the host's editor accent.
*/
void paintTabPendingEntryBox(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState* note,
    float center_x, float center_y, const juce::String& text, bool light_plate,
    juce::Colour text_color, juce::Colour border_color);

/*!
\brief Answers which note the lane draws at one index, for a host composing two projected forms.

The editor's actual-ring reveal is the one caller: it draws each note in the chart's presented or
its \ref common::core::ChartNoteForm::Actual form, decided per note, and the two forms of one chart
align by index because presentation trims tails and never adds or removes a note. Handing the
choice in as an accessor keeps the composition rule wholly in the host — this core is given the
note to draw and never the reason.

An empty accessor is the ordinary case: every note draws from the state's own
\ref common::core::ChartViewState::notes.

It is asked once per VISIBLE note in each of the two note passes — the range cull runs first, on
the state's own onsets — so the indirection costs a call per note drawn, never one per glyph.
*/
using TabDrawnNote = std::function<const common::core::NoteViewState&(std::size_t index)>;

/*!
\brief Answers whether one note's whole truth is on show, for a host that reveals notes.

The OTHER half of the one per-note pick \ref TabDrawnNote answers: the same reveal that hands this
core a note's real ring is what brings its reveal-only marks in (\ref common::core::stopMarkShown),
because revealing a note shows the whole truth about it at once. A host derives both from ONE
predicate of its own — this core is told the answer and never the reason, exactly as it is for the
form.

An empty accessor is the ordinary case and means nothing is revealed, which is the whole answer for
a surface with no reveal at all: the game's tab strips, and any host drawing one form throughout.
*/
using TabRevealedNote = std::function<bool(std::size_t index)>;

/*!
\brief Returns the panel \ref drawTabStringLegend would fill, empty when no legend is drawn.

The legend's own geometry, and THE ONE authority on the panel's width. A host that pins the panel
reads this rectangle for everything it does with the column: exclude the lane's content from it,
lay the tint, draw the names, repaint exactly where the panel was and where it now is, hit-test it
as inert chrome, and hand it to the canvas beneath so the grid stops there too. A host measuring it
itself would be a second authority on a width this core derives from the lane's font. It spans the
lane's FULL HEIGHT, gaps between strings included: the panel is one pane over the whole lane, not
six patches.

THE WIDTH IS TUNING-INDEPENDENT, deliberately: it holds the widest note name the display can ever
state — every letter, in both accidental spellings, with an octave digit — so retuning a song
cannot move the panel, and neither can scrolling to a passage on a different chart. That costs a
few pixels of width against measuring the tuning at hand, and buys a panel that never moves under
the reader. Measure it when the lane's font changes and cache it; it is not a per-frame question.

\param metrics Metrics from makeTabLaneMetrics for the lane being labelled.
\param open_strings The tuning's open-string names; only whether it is EMPTY is read, since a lane
       with no names to print gets no panel and the width does not depend on the names.
\param left_x Left edge of the panel, in the metrics' bounds space.
\return The panel's rectangle, or an empty one where the legend draws nothing.
*/
[[nodiscard]] juce::Rectangle<int> tabStringLegendBounds(
    const TabLaneMetrics& metrics, const std::vector<std::string>& open_strings, int left_x);

/*!
\brief Lays the string legend's tint over the panel column: the ground its names are read on.

THE PANEL IS AN EXCLUSION PLUS A TINT (user ruling 2026-09-03), not a scrim laid over finished
notation. The host takes the panel column out of the clip for every lane-content mark it draws —
the notation is not quieted there, it is absent — and lays this tint over whatever the surface
BEHIND the lane painted, so the reader sees the canvas's own ink (in the editor, the waveform)
through the column instead of a stripe that erased it.

The strength is this core's sighting knob, deliberately kept where the legend lives: at full the
column reads as an opaque stretch of the host's own row band, and lower settings reveal more of
what the canvas painted. Notation stays fully excluded at EVERY setting, so the knob moves one
thing only.

Laid BEFORE the lane's content rather than after it. Inside the column that is the same picture —
the content is excluded there — and outside it the tint does not exist at all, so the order is a
statement about the column's ground and never about what covers a mark.

\param g Graphics context to draw into.
\param panel The panel's rectangle from \ref tabStringLegendBounds, slid to where the host pins it;
       an empty one draws nothing.
\param ground Colour the tint is mixed from: the host's own lane band, so the panel reads as a
       quieted stretch of that band rather than as a foreign surface.
*/
void drawTabStringLegendTint(juce::Graphics& g, juce::Rectangle<int> panel, juce::Colour ground);

/*!
\brief Draws the tuning's open-string names ON their own lane lines, inside the pinned panel.

The legend a reader needs to know which line is which string: each name in its OWN string's colour,
sitting on that string's line at the fret digits' size, inside the panel the host places. Drawn
LAST, over the tint and over the furniture that crosses the column, which is what makes the letters
readable at every scroll position rather than only where the lane happens to be empty.

Ink only: the ground under the names is \ref drawTabStringLegendTint's, laid before the lane's
content, because the column's ground and its letters sit on opposite sides of everything the lane
draws between them.

The names are \ref common::core::ChartViewState::open_strings verbatim, which is the chart tuning's
own array: a drop or altered tuning names its strings and this prints what it named. Nothing here
converts a pitch — the spelling question belongs to whoever wrote the tuning.

The host owns WHERE the panel sits, because a lane inside a scrolling canvas and a lane sized to
its window need different answers, and neither is a fact this core can see. What it does not own is
the drawing: the ink and where each name sits are this core's, so the legend cannot drift from the
notation it labels.

\param g Graphics context to draw into.
\param metrics Metrics from makeTabLaneMetrics for the lane being labelled.
\param open_strings The tuning's open-string names, lowest string first; empty draws nothing.
\param panel The panel's rectangle from \ref tabStringLegendBounds, slid to where the host pins it;
       an empty one draws nothing.
*/
void drawTabStringLegend(
    juce::Graphics& g, const TabLaneMetrics& metrics, const std::vector<std::string>& open_strings,
    juce::Rectangle<int> panel);

/*!
\brief Returns the box one fret-hand-position chip fills with its left edge at \p left_x.

The chip's geometry, exported for the same reason the legend panel's is: a host that PINS a chip
needs its width before it draws it — the pin yields to an incoming marker that comes within a
label's clearance of it — and a host measuring the text itself would be a second authority on a
width derived from this lane's own label font.

\param metrics Metrics from makeTabLaneMetrics for the lane the chip rides.
\param fhp The placement the chip states.
\param left_x Left edge of the chip, in the metrics' bounds space.
\return The chip's box, which is empty on a lane that prints no text.
*/
[[nodiscard]] juce::Rectangle<float> tabFhpChipBounds(
    const TabLaneMetrics& metrics, const common::core::FhpViewState& fhp, float left_x);

/*!
\brief Draws one fret-hand-position chip with its left edge at \p left_x.

ONE chip drawer, two callers, because the mark is the same mark either way (user ruling
2026-09-03): \ref paintTabLaneFurniture draws every visible placement at its own column, and a host
that pins the GOVERNING placement to the window's left edge draws the ordinary chip there. Taking
the column as a parameter rather than reading it off the placement is what keeps the pinned chip
from becoming a second drawing of the same thing.

\param g Graphics context to draw into.
\param metrics Metrics from makeTabLaneMetrics for the lane the chip rides.
\param fhp The placement the chip states.
\param left_x Left edge of the chip, in the metrics' bounds space.
*/
void drawTabFhpChip(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::FhpViewState& fhp,
    float left_x);

/*!
\brief Draws one tablature lane's visible chart content in Charter's layer order.

String lines, sustain tails with their slide and bend lines, arpeggio posture brackets, note heads
with technique glyphs, then the floating labels (slide frets and bend amount chips) on top.
Visibility is bounded by the graphics context's clip region widened by head slack, so hosts repaint
partial regions (tile strips, dirty rectangles) correctly.

This is the lane's CONTENT — the marks that stand for chart events at their own instants. The
span rails, the capo chip and the fret-hand chips are \ref paintTabLaneFurniture's, drawn by a
second call so a host can put something between the two layers; a host wanting the whole lane
simply calls them back to back.

A host that pins chrome over the lane (the editor's string legend) excludes that column from the
clip around this call, which is why nothing here knows the panel: an exclusion in the context is
one statement covering every mark below, where a rectangle passed in would have to be honoured by
each pass in turn.

\param g Graphics context to draw into; its clip bounds gate the visible span.
\param metrics Metrics from makeTabLaneMetrics for the lane being painted.
\param tab Seconds-resolved tab projection; it must name at least one string. Its notes order and
       count the lane's notes, and supply every one of them unless `drawn_note` picks another
       form's. Every tail is drawn to the DRAWN note's own end (NoteViewState::end_seconds); the
       span-implied hold (ChartViewState::display_hold_ends) is the 3D board's and is not read
       here.
\param prefix_max_end_seconds Running maximum of note ends (common::core::makeSustainPrefixMax)
       bounding the visible range. It must reach at least as far as every end DRAWN or a tail on
       screen is culled away, so a host composing two forms passes the table of the form whose
       tails run longest — conservative for both, since the passes below drop each note that
       really ends before the span.
\param prefix_max_shape_end_seconds The same running maximum over `tab.shapes`, bounding the two
       span passes. Nothing orders spans by END, so without it those passes start at the first span
       in the song and walk the whole prefix on every repaint. An EMPTY table is legal and means
       exactly that — the index only ever tightens the start, never changes which spans draw — so a
       caller with no reason to build one simply does not.
\param drawn_note Per-index choice of which form's note to draw; empty draws `tab.notes`
       throughout.
\param revealed Per-index answer to whether that note's whole truth is on show, which is what a
       reveal-only held-stop satellite waits for; empty reveals nothing.
*/
void paintTabLane(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const std::vector<double>& prefix_max_end_seconds,
    const std::vector<double>& prefix_max_shape_end_seconds = {},
    const TabDrawnNote& drawn_note = {}, const TabRevealedNote& revealed = {});

/*!
\brief Draws one tablature lane's furniture: the span rails, the capo chip, the fret-hand chips.

The marks that state what is IN FORCE across a stretch rather than what happens at an instant, and
the reason they are their own pass: a host laying chrome over the lane draws them ABOVE it (user
ruling 2026-09-03), because a span running under a pinned panel is still in force there and a
reader looking at the panel's column has to be able to see so. Everything \ref paintTabLane draws
goes under that chrome instead.

Called once per paint, straight after \ref paintTabLane on a host with no chrome to interleave.
Its own visible range comes from the graphics context, exactly as the content pass's does, so a
host that narrowed the clip for the content pass gets the matching furniture for free.

\param g Graphics context to draw into; its clip bounds gate the visible span.
\param metrics Metrics from makeTabLaneMetrics for the lane being painted.
\param tab Seconds-resolved tab projection supplying the spans, the capo and the placements.
\param prefix_max_shape_end_seconds Running maximum over `tab.shapes` bounding the rail pass, with
       the same meaning it has for \ref paintTabLane: an empty table starts the pass at the first
       span in the song rather than changing which spans draw.
*/
void paintTabLaneFurniture(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const std::vector<double>& prefix_max_shape_end_seconds = {});

} // namespace rock_hero::common::ui
