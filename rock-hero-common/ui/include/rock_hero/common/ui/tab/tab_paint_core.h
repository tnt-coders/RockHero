/*!
\file tab_paint_core.h
\brief Shared JUCE notation paint core rendering the 2D tablature for both products.

The one designated juce_graphics-bearing public header in rock-hero-common/ui (the 30-Q1
amendment in docs/design/architectural-principles.md "UI Modules"): the editor tab lane and the
game tab strips must produce identical notation pixels, so the rasterizer itself is shared and
each host supplies only bounds, timeline mapping, and state.
*/

#pragma once

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

Tuned on this surface for its own reason, as the board tunes
\ref rock_hero::common::core::g_highway_wide_vibrato_depth_multiplier for its own; the two need not
agree. Stated once and read in BOTH directions here: the wide wave fills the tail's technique band
and the ordinary one is that divided by this, because the band is a hard clip on this surface — a
wave drawn past it would truncate its crests and read as a square wave rather than as a wider
vibrato, which is why the lane cannot scale the wide tier up the way the board does.
*/
inline constexpr float g_wide_vibrato_swing_multiplier = 2.0f;

/*!
\brief The corner rounding every lane chip shares, boxed or floating — and a ring a host traces
around one, so the ring follows the plate it surrounds.
*/
inline constexpr float g_lane_chip_corner_radius = 2.0f;

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
fret at the junctions). The text itself comes from the one label authority both surfaces share
(`chartStopText` over `soundingStopAt`), which the posture bracket and the 3D floor print through
too.

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

    /*!
    \brief Bold fret-number font derived from the note height, and the text of every lane chip —
    fret-hand, capo, slide label and bend amount — so chips scale with the lane as the digits do.
    */
    TabLaneFont fret_font;

    /*!
    \brief The fret font scaled up for a bend amount's vulgar-fraction glyph, whose own digits are
    drawn small: at this size they match a whole number's weight beside them.
    */
    TabLaneFont fraction_font;

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
\brief A bend amount in Charter's notation — whole steps with quarter fractions ("0", "1/2",
"1 1/4", ...) — split into the whole steps and the vulgar-fraction glyph after them.

THE one spelling of an amount, read by the lane's bend chips, which print the two parts in
different fonts, and by a host's menus, which join them; so a row and the chip it produces read the
same. An amount rounds to the nearest quarter step.
*/
struct TabBendAmountText
{
    /*! \brief The whole steps, with a trailing space where a fraction follows; "0" at rest. */
    juce::String text;

    /*! \brief The quarter-step glyph (1/4, 1/2 or 3/4), empty on a whole step. */
    juce::String fraction;
};

/*!
\brief Spells a bend amount in Charter's notation (\ref TabBendAmountText).
\param semitones The amount, in semitones.
\return The whole steps and the fraction glyph.
*/
[[nodiscard]] TabBendAmountText tabBendAmountText(double semitones);

/*!
\brief Redraws one keyframe's LINKED HEAD — the mark and its digit — over whatever is already there.

THE SELECTED OBJECT DRAWS LAST: the lane paints in chart order, so an ARRIVAL at a head's own
instant is covered by that head and its accent ring would sit around a mark nobody can read. Drawn
through the very drawers the lane used, so the redrawn mark cannot differ from the committed one.
Nothing is drawn for a keyframe that wears no head (\ref common::core::keyframeHeadFret).

\param g Graphics context to draw into.
\param metrics Metrics of the lane being painted.
\param note Note the keyframe rides.
\param keyframe The keyframe whose linked head is redrawn.
*/
void paintTabKeyframeHead(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState& note,
    const common::core::KeyframeViewState& keyframe);

/*!
\brief Draws the editor's pending entry plate on a given box — THE polarity rule of a provisional
value, whatever mark it rides.

Host chrome, not notation — the game renders no keyboard entry — but exported from the core so
every pending box draws one way: the GROUND flips with validity, and both grounds are known and
internal. A valid value sits on the lane's near-black exactly like a committed plated digit, and
an invalid one flips to a white plate — red-on-white is the error idiom at full contrast, and the
plate polarity flip itself carries the signal in full monochrome, the glance mechanism the mute
plate-flip design established. The border and inks are the host's, because pending is an editor
state and this core owns no editor colors.

The box and font are the caller's, because they are the MARK's: a head's digit plate in the fret
font (\ref paintTabPendingEntryBox), or a fret-hand chip's box, which prints in the fret font too
(\ref tabFhpChipBounds), so the provisional value sits exactly where the committed one prints.

\param g Graphics context to draw into.
\param metrics Metrics of the lane being painted; a lane that prints no text draws no text here.
\param font The font the mark's committed value prints in.
\param plate The box the mark's value fills.
\param text The text the plate carries.
\param light_plate True flips the plate to the white invalid ground; false is the dark valid one.
\param text_color Text ink: the host's digit white while the value would apply, red when not.
\param border_color Plate border: the host's editor accent.
*/
void paintTabPendingEntryPlate(
    juce::Graphics& g, const TabLaneMetrics& metrics, const TabLaneFont& font,
    juce::Rectangle<float> plate, const juce::String& text, bool light_plate,
    juce::Colour text_color, juce::Colour border_color);

/*!
\brief Draws the editor's pending fret entry box over a head or an empty slot: the mute
number-plate's own geometry and font carrying a provisional value, in
\ref paintTabPendingEntryPlate's polarity.

Exported rather than restated in the host so the provisional digit's typography and placement
CANNOT drift from the committed head's (the one-primitive rule of the pending-entry design; the
insert ghost's shape drifted exactly this way once). The plate rect is the same authority the mute
number-plate draws and the font is the head digit's own.

The box follows the head's own digit PLACEMENT too: a plectrum raises its number to fit the
silhouette, so the box over a scrape rides the same raise — the provisional digit must sit
exactly where the committed one will land.

\param g Graphics context to draw into.
\param metrics Metrics of the lane being painted.
\param note Note whose head the box rides, or null where no head stands (an empty insert slot);
       supplies the head shape the digit placement follows.
\param center_x Box center on the time axis — a head's onset x, or a slot's.
\param center_y The head's center on the lane's string line; the box derives the digit's own
       center from it.
\param text Provisional value exactly as typed.
\param light_plate True flips the box to the white invalid ground; false is the dark valid one.
\param text_color Text ink: the host's digit white while the value would apply, red when not.
\param border_color Box border: the host's editor accent.

\return The plate the box drew on. Returned because a host offering a CHOICE has a second value to
        show beside the armed one — the harmonic picker's unchosen node — and its baseline is this
        plate's, not the string line's: a digit raised onto a plectrum's broad band would otherwise
        sit a few pixels above its own alternative. The host draws that label itself, in its own
        ink, because pending is an editor state and this core owns no editor colors.
*/
juce::Rectangle<float> paintTabPendingEntryBox(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState* note,
    float center_x, float center_y, const juce::String& text, bool light_plate,
    juce::Colour text_color, juce::Colour border_color);

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

THE PANEL IS AN EXCLUSION PLUS A TINT, not a scrim laid over finished notation. The host takes the
panel column out of the clip for every lane-content mark it draws — the notation is not quieted
there, it is absent — and lays this tint over whatever the surface BEHIND the lane painted, so the
reader sees the canvas's own ink (in the editor, the waveform) through the column instead of a
stripe that erased it.

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
\brief Paints a bend point's chip exactly as the lane does, and returns the plate it filled.

THE SELECTED OBJECT DRAWS LAST: every chip stands on its true column, so chips meet (an ending
ring's chip and the pre-bend chip of the head it ends on; points close together), the lane painting
the later on top. A host repaints a selected point's chip over whatever covers it before tracing
its ring on the returned plate. Built by the lane's own routine, so the repainted chip is the lane's
chip; the plate is the measured text, narrower than the click box, so the ring claims the chip's
own extent.

\param g Graphics context to paint into.
\param metrics Metrics from makeTabLaneMetrics for the lane the chip rides.
\param note The note the point belongs to.
\param point Index of the point in the note's \ref common::core::NoteViewState::bend.
\param box The chip's layout box (\ref tabBendPointChipBox).
\return The plate's bounds, in the metrics' bounds space.
*/
juce::Rectangle<float> paintTabBendChip(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState& note,
    std::size_t point, const TabLayoutRect& box);

/*!
\brief Paints a slide stop's chip exactly as the lane does, and returns the plate it filled: the
slide twin of \ref paintTabBendChip.
\param g Graphics context to paint into.
\param metrics Metrics from makeTabLaneMetrics for the lane the chip rides.
\param note The note the stop belongs to.
\param stop Index of the stop in the note's \ref common::core::NoteViewState::slides.
\param box The stop layout's box (\ref tabSlideStopLayout).
\return The plate's bounds, in the metrics' bounds space.
*/
juce::Rectangle<float> paintTabSlideChip(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::NoteViewState& note,
    std::size_t stop, const TabLayoutRect& box);

/*!
\brief THE ONE STATEMENT of what a fret-hand-position chip says: the index-finger fret for the
standard four-fret hand, the full inclusive range ("3-7") for a wider or narrower one.

Exported so a host drawing over a chip — the editor's pending fret entry — prints exactly the text
the chip itself prints, in the box \ref tabFhpChipBounds measured for it.

\param fhp The placement the chip states.
\return The chip's text.
*/
[[nodiscard]] juce::String tabFhpChipText(const common::core::FhpViewState& fhp);

/*!
\brief Draws one fret-hand-position chip with its left edge at \p left_x.

ONE chip drawer, two callers, because the mark is the same mark either way: \ref
paintTabLaneFurniture draws every visible placement at its own column, and a host that pins the
GOVERNING placement to the window's left edge draws the ordinary chip there. Taking the column as a
parameter rather than reading it off the placement is what keeps the pinned chip from becoming a
second drawing of the same thing.

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
\param tab Seconds-resolved tab projection; it must name at least one string. Every tail is drawn
       to the extent \ref drawnExtentSeconds names; the span-implied hold
       (ChartViewState::display_hold_ends) is the 3D board's and is not read here. The visible
       range is bounded by the projection's own prefix tables (ChartViewState::ring_end_prefix_max,
       ChartViewState::shape_close_prefix_max).
\param presence Per-note presentation (\ref TabNotePresence). Its reveal at 1 draws the note to its
       ring end, every keyframe at its true instant; between, it draws that far, the marks and
       chips riding the extent there; above 0 its tail stops fading at the crop. Its recede above 0
       draws the whole note — head, digit, tail and chips — fading toward a fifth of its weight,
       beneath every other note. Empty presents every note plainly: it crops at its ink end, and
       the leg the crop cuts wears a destination chip there (\ref tabKeyframeLayout).
\param ground The colour the host painted under the lane. The tail's core is light laid over it
       (\ref common::core::g_tail_core_alpha), and the one mark that must knock out what lies
       beneath — a bracket's satellite digit's ground — restores this colour before laying the core
       back over it, so the knockout reads as a clean stretch of the tail. Transparent where the
       host composites the lane itself.
*/
void paintTabLane(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const TabPresence& presence = {}, juce::Colour ground = juce::Colours::transparentBlack);

/*!
\brief Draws one tablature lane's furniture: the span rails, the capo chip, the fret-hand chips.

The marks that state what is IN FORCE across a stretch rather than what happens at an instant, and
the reason they are their own pass: a host laying chrome over the lane draws them ABOVE it, because
a span running under a pinned panel is still in force there and a reader looking at the panel's
column has to be able to see so. Everything \ref paintTabLane draws goes under that chrome instead.

Called once per paint, straight after \ref paintTabLane on a host with no chrome to interleave.
Its own visible range comes from the graphics context, exactly as the content pass's does, so a
host that narrowed the clip for the content pass gets the matching furniture for free.

\param g Graphics context to draw into; its clip bounds gate the visible span.
\param metrics Metrics from makeTabLaneMetrics for the lane being painted.
\param tab Seconds-resolved tab projection supplying the spans, the capo and the placements.
\param revealed_shape Per-span answer to whether that span's rails run to its musical close instead
       of to the drawn extent; empty reveals nothing.
*/
void paintTabLaneFurniture(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const TabSpanRevealed& revealed_shape = {});

} // namespace rock_hero::common::ui
