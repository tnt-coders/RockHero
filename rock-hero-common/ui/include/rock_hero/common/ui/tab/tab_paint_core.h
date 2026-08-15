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
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <vector>

namespace rock_hero::common::ui
{

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
\param fret_at_head Fret the head being labeled sits at — `note.fret` at the onset, the waypoint's
       fret at a linked junction.
\return Head text, never empty.
*/
[[nodiscard]] juce::String tabNoteHeadText(const common::core::TabNoteView& note, int fret_at_head);

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
\brief Lane geometry plus the JUCE-only facts one paint call needs.

Extends the framework-free TabLaneGeometry (which layout-manifest consumers share) with the
component bounds and the derived fonts, so every drawer of one paint call reads one authority.
*/
struct TabLaneMetrics : TabLaneGeometry
{
    /*! \brief Full tablature lane bounds in the graphics context's space. */
    juce::Rectangle<int> bounds;

    // Initialized via FontOptions because JUCE 8 deprecates the default Font constructor; the
    // placeholder values are replaced by makeTabLaneMetrics before any drawing.

    /*! \brief Bold fret-number font derived from the note height. */
    juce::Font fret_font{juce::FontOptions{}};

    /*!
    \brief Bold arpeggio posture-fret font: \ref fret_font stepped down, floored at its own
    minimum.

    A posture digit states where the hand is held, never what sounds, so it is quieter than a
    fret number on a head. The step is small by design — at the standard lane it buys one pixel
    of cap height, and below a short lane it buys none because both fonts sit on the same floor —
    so the register is carried by the ink, not the size.
    */
    juce::Font posture_font{juce::FontOptions{}};

    /*! \brief Bend amount chip font derived from the note height. */
    juce::Font bend_font{juce::FontOptions{}};

    /*! \brief Bold label font for hand-shape and fret-hand-position chips. */
    juce::Font label_font{juce::FontOptions{}};

    /*!
    \brief Base color for a chart string, accounting for extra user lanes below the chart.
    \param chart_string One-based chart string, 1 = the chart's lowest string.
    \return Base lane color for the string.
    */
    [[nodiscard]] juce::Colour baseColor(int chart_string) const;
};

/*!
\brief TEMPORARY EXPERIMENT: which arpeggio posture-display candidate the lane paints.

Scaffolding so the candidates can be judged in the real editor rather than in a rendering harness —
four rounds of measurement missed both objections that live use caught (a sustain ribbon crossing
the mark, and a digit outside the brackets reading as detached from them), because neither is a
contrast problem. DELETE this enum, \ref arpeggioPostureVariant, \ref setArpeggioPostureVariant,
their uses in the bracket pass, and the editor's cycle command once a candidate is chosen.
*/
enum class ArpeggioPostureVariant : std::uint8_t
{
    /*! \brief Outboard right of the closing bar, muted grey, 1 px casing (what shipped). */
    SatelliteCased,
    /*! \brief The same slot with no casing — isolates whether the casing is what hurts. */
    SatelliteBare,
    /*! \brief The same slot at full fret size — isolates whether the size step is what hurts. */
    SatelliteFull,
    /*! \brief In the head's own centring box; a head sounding at the span start wins it. */
    CentredYield,
    /*!
    \brief Outboard right, in a box whose LEFT WALL IS the closing bracket bar.

    The satellite's defect was never its position — every string offsets identically — but that a
    bare digit beside a mark reads as detached from it. Sharing a wall makes the digit's container
    part of the bracket's own silhouette, so there is nothing left to read as separate.
    */
    ChipMerged,
    /*!
    \brief \ref ChipMerged with NO border — the backing alone.

    The border was doing two jobs and only needs to do one. Attachment is already carried by the
    bracket's own closing bar, which the backing starts at, so the rails add ink without adding
    meaning; what the digit actually needs from a container is a ground that a slide line, a bend
    curve or a tail cannot bleed through. Dropping them also returns their height to the digit.
    */
    ChipFillOnly,
    /*!
    \brief \ref ChipFillOnly with the digit at full fret-number size.

    It IS a fret number, so at the size a fret number is drawn it reads as one — and the height the
    dropped rails gave back is what makes room for it.
    */
    ChipFillLarge,
    /*!
    \brief The posture states centred, and takes the side slot ONLY where a tap displaces it.

    One rule covering every case, and the side chip stops being a permanent fixture:
    - Nothing sounds on the string at the span start: the posture states CENTRED, where a fret
      number belongs. It gets a tail-coloured ground only when a sustain is crossing that column,
      so a slide diagonal or bend curve running through cannot swallow it.
    - A head sounds there at the posture's own fret: the head already states it; nothing is added.
    - A head sounds there at a DIFFERENT fret from a picking-hand onset (\ref
      rock_hero::common::core::rightHandOnset): the tap is what rings, so it keeps the centre, and
      the still-held posture moves to the side chip. The fretting hand has not moved, which is
      exactly why its fret is still worth stating.
    - A head sounds there at a different fret from the FRETTING hand: the hand has left the
      template, so the posture is no longer held and claiming it would be false. Nothing is added.
    */
    PostureSmart,
    /*! \brief Count of variants, for cycling. */
    Count
};

/*!
\brief TEMPORARY EXPERIMENT: how the sustain tail is filled and bordered.

Everything drawn ON a tail — posture digits, slide labels, bend chips, mute marks, the vibrato
sine, tremolo gems — fights the tail's own brightness. Measured: the vibrato sine carries barely
6 dL* against the yellow string's tail, and that string's tail EDGE is lighter than the grey ink
crossing it, which inverts the contrast outright.

Every step past 0 keeps the edge at its shipped brightness and darkens only the FILL, because the
edge is what carries string identity and says "this rings" — so the fill can go much darker than a
uniform multiplier allowed. Those steps also drop the tail's right END CAP, since a tail that
simply stops has nothing to cap and the 3D highway draws none.

- 0: the shipped derivation, unchanged.
- 1, 2: the fill multiplied down, edge kept.
- 3: the fill set to the linked-note fill — a tail exactly as dark as the waypoint heads riding it.
*/
[[nodiscard]] int tailExperimentStep() noexcept;

/*!
\brief TEMPORARY: advances the tail experiment one step and returns it.
\return The step now in force; the caller repaints.

The step table lives beside the palette rather than in the caller, so the experiment has one
authority.
*/
int cycleTailExperiment() noexcept;

/*!
\brief TEMPORARY EXPERIMENT: which ink the arpeggio bracket marks are drawn in.

The brackets ship in `style.inner`, the note FILL colour, chosen so they mark the posture without
competing with real heads — a judgment made against the shipped BRIGHT tail. Once the tail
experiment darkens the fill toward the waypoint heads' own dark, that reasoning inverts and the
bracket goes dark-on-dark. The steps walk up the string's own palette rather than inventing a
colour: 0 the shipped fill, 1 the note ring, 2 the tail's own edge — the brightness already chosen
to read against a tail.
*/
[[nodiscard]] int bracketInkStep() noexcept;

/*!
\brief TEMPORARY: advances the bracket-ink experiment one step and returns it.
\return The step now in force; the caller repaints.
*/
int cycleBracketInk() noexcept;

/*!
\brief TEMPORARY: a short name for one bracket-ink step.
\param step Step to name.
\return Stable, human-readable name; never null.
*/
[[nodiscard]] const char* bracketInkName(int step) noexcept;

/*! \brief TEMPORARY: the posture variant the next paint will use. */
[[nodiscard]] ArpeggioPostureVariant arpeggioPostureVariant() noexcept;

/*!
\brief TEMPORARY: a short name for one posture variant, for reporting which one is on screen.
\param variant Variant to name.
\return Stable, human-readable name; never null.
*/
[[nodiscard]] const char* arpeggioPostureVariantName(ArpeggioPostureVariant variant) noexcept;

/*!
\brief TEMPORARY: a short name for one tail-experiment step.
\param step Step to name.
\return Stable, human-readable name; never null.
*/
[[nodiscard]] const char* tailExperimentName(int step) noexcept;

/*!
\brief TEMPORARY: selects the posture variant for subsequent paints.
\param variant Candidate to paint; the caller repaints.
*/
void setArpeggioPostureVariant(ArpeggioPostureVariant variant) noexcept;

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
    juce::Graphics& g, const common::core::TabNoteView& note, float center_x, float center_y,
    float extent, float stroke_thickness);

/*!
\brief Draws one tablature lane's visible chart content in Charter's layer order.

String lines, hand-shape spans, sustain tails with their slide and bend lines, arpeggio posture
brackets, note heads with technique glyphs, then the floating labels (slide frets and bend
amount chips) on top. Visibility is bounded by the graphics context's clip region widened by
head slack, so hosts repaint partial regions (tile strips, dirty rectangles) correctly.

\param g Graphics context to draw into; its clip bounds gate the visible span.
\param metrics Metrics from makeTabLaneMetrics for the lane being painted.
\param tab Seconds-resolved tab projection; string_count must be positive and display_hold_ends
       must be sized like notes — tails are drawn to the DISPLAY hold end, so the two travel
       together.
\param prefix_max_end_seconds Running maximum of `tab.display_hold_ends`
       (common::core::makeSustainPrefixMax), not of the notes' own sustain ends: a span-held strum
       is drawn past its stored end and would otherwise be culled out of the visible range.
*/
void paintTabLane(
    juce::Graphics& g, const TabLaneMetrics& metrics, const common::core::TabViewState& tab,
    const std::vector<double>& prefix_max_end_seconds);

} // namespace rock_hero::common::ui
